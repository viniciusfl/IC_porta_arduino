#ifndef FIRMWARE_H
#define FIRMWARE_H

void writeToFirmwareFile(const char* data, int data_len);
void performFirmwareUpdate();
void cancelFirmwareDownload();
#endif
