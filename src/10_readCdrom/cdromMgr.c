#include "cdromMgr.h"
#include "ps1/cdrom.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COMM_DELAY 0x1F801020
#define COMM_DELAY_INIT_VAL 0x1325

#define REG_0 0x1F801800
#define REG_1 0x1F801801
#define REG_2 0x1F801802
#define REG_3 0x1F801803

#define  HCHPCTL_SMEN  1 << 5 // Sound map (manual XA-ADPCM playback) enable
#define  HCHPCTL_BBFWR 1 << 6 // Request sector buffer write (prepare for writes to WRDATA)
#define  HCHPCTL_BFRD  1 << 7 // Request sector buffer read (prepare for reads from RDDATA)

//TODO consider caching last bank
void cdrom_set_bank(unsigned bank){
    *(volatile uint8_t *)REG_0 = bank;
}

static inline uint8_t cdrom_reg0_r(void) { //read to reg0
    return *(volatile uint8_t *)REG_0;
}
static inline uint8_t cdrom_reg1_r(void) { //read to reg1
    return *(volatile uint8_t *)REG_1;
}
static inline uint8_t cdrom_reg2_r(void) { //read to reg2
    return *(volatile uint8_t *)REG_2;
}
static inline uint8_t cdrom_reg3_r(void) { //read to reg3
    return *(volatile uint8_t *)REG_3;
}
static inline void cdrom_reg0_w(uint8_t val) { //write to reg0
    *(volatile uint8_t *)REG_0 = val;
}
static inline void cdrom_reg1_w(uint8_t val) { //write to reg1
    *(volatile uint8_t *)REG_1 = val;
}
static inline void cdrom_reg2_w(uint8_t val) { //write to reg2
    *(volatile uint8_t *)REG_2 = val;
}
static inline void cdrom_reg3_w(uint8_t val) { //write to reg3
    *(volatile uint8_t *)REG_3 = val;
}
static inline void comm_delay_w(uint32_t val) { //write to reg3
    *(volatile uint8_t *)COMM_DELAY = val;
}

static inline void cdrom_clearIRQ(void)
{
    cdrom_set_bank(1);
    cdrom_reg3_w(0x1f); // HCLRCTL Host Clear Control
}

static inline CDROMIRQType cdrom_IRQ_r(void)
{
    cdrom_set_bank(1);
    return cdrom_reg3_r() & 0b00000111;
}

static inline void cdrom_clearHCHPCTL(void)
{
    cdrom_set_bank(0);
    cdrom_reg3_w(0); // HCHPCTL Host Chip/Channel Control
}

static inline void cdrom_command(uint8_t command)
{
    cdrom_set_bank(0);
    cdrom_reg1_w(command);
}

static inline void cdrom_ack(void) { // acknowledge
    cdrom_set_bank(1);
    cdrom_reg3_w(0b1111);
}

#define IRQ_ERROR_0xFF

static uint8_t cdrom_waitIRQ(uint8_t target) {
    for (;;) {
        CDROMIRQType irq = cdrom_IRQ_r();

        if (irq == target) {
            cdrom_set_bank(0);
            uint8_t result = cdrom_reg1_r();
            //TODO expand
            cdrom_ack();
            return result;
        }

        if (irq == CDROM_IRQ_ERROR) {
            cdrom_set_bank(0);

            uint8_t stat  = cdrom_reg1_r();
            uint8_t error = cdrom_reg1_r();
            printf("[ERROR] CDROM ERROR: Stat %#08x, error %#08x\n", stat, error); //todo logging standarization //todo 0x23 0x0 when no ISO
            cdrom_ack();

            return 0xff; 
        }
    }
}

void cdrom_setMode2048() {
    cdrom_set_bank(0);
    cdrom_reg2_w(CDROM_MODE_SIZE_2048);
    cdrom_reg1_w(CDROM_CMD_SETMODE);
    cdrom_waitIRQ(CDROM_IRQ_ACKNOWLEDGE);
}

void cdrom_init() {
    cdrom_clearIRQ();
    cdrom_clearHCHPCTL();
    comm_delay_w(0x00001325); //todo investigate value further

    cdrom_command(CDROM_CMD_NOP);
    cdrom_command(CDROM_CMD_NOP);
    cdrom_command(CDROM_CMD_INIT);

    cdrom_waitIRQ(CDROM_IRQ_ACKNOWLEDGE);
    cdrom_waitIRQ(CDROM_IRQ_COMPLETE);

    cdrom_setMode2048();
}

bool cdrom_setlocLBA(uint32_t lba) {
    CDROMMSF msf;

    cdrom_convertLBAToMSF(&msf, lba);

    cdrom_set_bank(0);
    cdrom_reg2_w(msf.minute);
    cdrom_reg2_w(msf.second);
    cdrom_reg2_w(msf.frame);
    cdrom_command(CDROM_CMD_SETLOC);
    cdrom_waitIRQ(CDROM_IRQ_ACKNOWLEDGE);

    printf("LBA=%u -> MSF=%02X:%02X:%02X\n",
       lba,
       msf.minute,
       msf.second,
       msf.frame);

    return true;
}

bool cdrom_readSector(uint8_t *buffer) {
    cdrom_command(CDROM_CMD_READ_N);
    cdrom_waitIRQ(CDROM_IRQ_ACKNOWLEDGE);
    cdrom_waitIRQ(CDROM_IRQ_DATA_READY);

    cdrom_set_bank(0);

    // Enable reading from the CD-ROM data buffer.
    cdrom_reg3_w(HCHPCTL_BFRD);

    for (int i = 0; i < 2048; i++)
        buffer[i] = cdrom_reg2_r();

    cdrom_command(CDROM_CMD_PAUSE);
    cdrom_waitIRQ(CDROM_IRQ_ACKNOWLEDGE);
    cdrom_waitIRQ(CDROM_IRQ_COMPLETE);

    return true;
}

static uint32_t readISOu32(const uint8_t *p) {
    return ((uint32_t)p[0] << 0 )  |
           ((uint32_t)p[1] << 8 )  |
           ((uint32_t)p[2] << 16)  |
           ((uint32_t)p[3] << 24);
}

typedef struct {
    uint32_t lba;
    uint32_t size;
    uint8_t flags;
    const uint8_t *name;
    uint8_t nameLength;
} ISODirEntry;

#define ISO_PVD_LBA 16 // ISO primary volume descriptor
#define ISO_FLAG_HIDDEN       0x01
#define ISO_FLAG_DIRECTORY    0x02
#define ISO_FLAG_ASSOCIATED   0x04
#define ISO_FLAG_RECORD       0x08
#define ISO_FLAG_PROTECTION   0x10
#define ISO_FLAG_MULTI_EXTENT 0x80


bool cdrom_getEntryAtLBA(uint32_t lba, uint8_t* sectorBuf, ISODirEntry* entry) {
    if (!cdrom_setlocLBA(lba))
        return false;

    if (!cdrom_readSector(sectorBuf))
        return false;
    
    puts("Read sector");
    uint8_t sectorOffset = lba == ISO_PVD_LBA ? 156 : 0;

    uint8_t* sectorBufOffset = sectorBuf + sectorOffset;

    printf("type: %u\n", sectorBufOffset[0]);
    printf("id: %.5s\n", &sectorBufOffset[1]);
    printf("version: %u\n", sectorBufOffset[6]);
    printf("volume: %.32s\n", &sectorBufOffset[40]);
    printf("lba: %d\n", &sectorBufOffset[2]);

    entry->lba        = readISOu32(&sectorBufOffset[2]);
    entry->size       = readISOu32(&sectorBufOffset[10]);
    entry->flags      = sectorBufOffset[25];
    entry->nameLength = sectorBufOffset[32];

    if (lba == ISO_PVD_LBA) {
        //Enter Root Directory Record
        if (!cdrom_setlocLBA(entry->lba))
            return false;
        return cdrom_readSector(sectorBuf);
    }

    return false;
}

bool cdrom_checkFolderForEntry(const char* fileName, ISODirEntry* folderEntry, uint8_t* sectorBuf, ISODirEntry* outEntry) {
    if(!(folderEntry->flags & ISO_FLAG_DIRECTORY)) {
        puts("[CDROM] ERROR - not a folder");
        return false;
    }
    uint32_t pos = 0;
    uint32_t folderSize = folderEntry->size;
    while (pos < folderSize) {        
        const uint8_t *entryRaw = &sectorBuf[pos];
        uint8_t entryLength = entryRaw[0];
        // Unused remainder of the sector.
        if (entryLength == 0)
            break;

        outEntry->lba        = readISOu32(&entryRaw[2]);
        outEntry->size       = readISOu32(&entryRaw[10]);
        outEntry->flags      = entryRaw[25];
        outEntry->nameLength = entryRaw[32];

        printf("Searching entry: \nLBA=%u \nsize=%u \nflags=%02X \nname=",
               outEntry->lba, outEntry->size, outEntry->flags);

        if (outEntry->nameLength == 1 && entryRaw[33] == 0) {
            printf(".");
        } else if (outEntry->nameLength == 1 && entryRaw[33] == 1) {
            printf("..");
            if (memcmp("..", fileName, 2) == 0) {
                printf("\n");

                //todo printf("Found entry: %s\n", fileName);
                return true; // found entry
            }
        } else {
            for (uint32_t i = 0; i < outEntry->nameLength; i++) {
                printf("%c",entryRaw[33 + i]);
            }
            if (outEntry->nameLength == strlen(fileName) && memcmp(&entryRaw[33], fileName, outEntry->nameLength) == 0) {
                printf("\n");

                //todo printf("Found entry: %s\n", fileName);
                return true; // found entry
            }
        }

        printf("\n");
        pos += entryLength;
    }

    printf("Couldn't find the entry requested: %s\n", fileName);
    return false; // didn't find entry
}

bool cdrom_openFolder(const char* target, uint8_t* sectorBuf, ISODirEntry* cwd) {
    printf("Enter folder: %s\n", target);
    ISODirEntry cwdTemp;
    if(cdrom_checkFolderForEntry(target, cwd, sectorBuf, &cwdTemp))
    {
        cdrom_getEntryAtLBA(cwdTemp.lba, sectorBuf, cwd);
    } else {
        return false;
    }
    return true;
}

bool cdrom_readFile(ISODirEntry* fileEntry, uint8_t* sectorBuf, uint8_t* fileBuf) {
    if(fileEntry->flags & ISO_FLAG_DIRECTORY) {
        puts("[CDROM] ERROR - not a file");
        return false;
    }
    uint32_t fileBufCurrIdx = 0;
    
    uint32_t remainingSize = fileEntry->size;
    
    if(fileEntry->size <= 2048) { 
        cdrom_setlocLBA(fileEntry->lba);
        cdrom_readSector(sectorBuf);
        
    for (uint32_t i = 0; i < remainingSize; i++, fileBufCurrIdx++)
        fileBuf[fileBufCurrIdx] = sectorBuf[i];
    } else {
        uint8_t iterations = fileEntry->size / 2048;
        if (fileEntry->size % 2048 > 0) {
            iterations++;
        }
    
        for (uint32_t i = 0; i < iterations; i++) {
            cdrom_setlocLBA(fileEntry->lba + i);
            cdrom_readSector(sectorBuf);
            for (uint32_t j = 0; j < ( remainingSize > 2048 ? 2048 : remainingSize ) ; j++, fileBufCurrIdx++) {
                fileBuf[fileBufCurrIdx] = sectorBuf[j];
            }
            remainingSize -= 2048;
        }
    }
    return true;
}


bool cdrom_readTest(uint8_t** outBuf, uint32_t* outLength) {
    cdrom_setMode2048();
    uint8_t sectorBuf[2048];
    ISODirEntry cwd;
    cdrom_getEntryAtLBA(ISO_PVD_LBA, sectorBuf, &cwd);  // Root Directory Record

    const char *odysseyFileName = "ODYSSEY.TXT;1";
    const char *iliadFileName = "ILIAD.TXT;1";
    
    ISODirEntry fileEntry;
    cdrom_openFolder("EPIC", sectorBuf, &cwd);
    cdrom_openFolder("ODYSSEY", sectorBuf, &cwd);
    if (cdrom_checkFolderForEntry(odysseyFileName, &cwd, sectorBuf, &fileEntry)) { //The file is not included, so it would omit this code
        uint8_t fileBuf[fileEntry.size];
        uint8_t sectorBuf_aux[2048];
        if(cdrom_readFile(&fileEntry, sectorBuf_aux, fileBuf)) {
            for (uint32_t i = 0; i < fileEntry.size; i++) {
                putchar(fileBuf[i]);
            }
            putchar('\n');
        }
    }

    cdrom_openFolder("..", sectorBuf, &cwd); // Whoops, wrong folder - thankfully we can go one up
    if (cdrom_checkFolderForEntry(iliadFileName, &cwd, sectorBuf, &fileEntry)) {
        uint8_t fileBuf[fileEntry.size];
        if(cdrom_readFile(&fileEntry, sectorBuf, fileBuf)) {
            *outLength = fileEntry.size;
            (*outBuf) = fileBuf;
            
            for (uint32_t i = 0; i < fileEntry.size; i++) {
                putchar(fileBuf[i]);
            }
            putchar('\n');
            return true;
        }
    }
    return false;
}