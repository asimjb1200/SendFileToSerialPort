#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <termios.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "SendFileToPort.h"

#define ACK 0x06
#define DONE 0x11

using namespace std;

int openSerialPort(const char* portname)
{
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        cerr << "Error opening " << portname << ": " << strerror(errno) << endl;

        return -1;
    }

    return fd;
}

// function to configure the serial port
bool configureSerialPort(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        cerr << "Error from tcgetattr: " << strerror(errno)
             << endl;
        return false;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit characters
    tty.c_iflag &= ~IGNBRK; // disable break processing
    tty.c_lflag = 0; // no signaling chars, no echo, no
                     // canonical processing
    tty.c_oflag = 0; // no remapping, no delays
    tty.c_cc[VMIN] = 0; // read doesn't block
    tty.c_cc[VTIME] = 5; // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD); // enable reading
    tty.c_cflag &= ~(PARENB | PARODD); // shut off parity
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        cerr << "Error from tcsetattr: " << strerror(errno)
             << endl;
        return false;
    }

    return true;
}

// Function to read data from the serial port
int readFromSerialPort(int fd, char* buffer, size_t size)
{
    return read(fd, buffer, size);
}

// Function to write data to the serial port
int writeToSerialPort(int fd, const char* buffer, size_t size)
{
    return write(fd, buffer, size);
}

// Function to close the serial port
void closeSerialPort(int fd) { close(fd); }

void responseFromChip(int serialPort)
{
    bool ackReceived = false;
    uint8_t chipResponse = 0x00;
    while (!ackReceived)
    {

        readFromSerialPort(serialPort, (char *)&chipResponse, 1);
        
        if (chipResponse == 0x15)
        {
            cout << "transfer failed\n";
        }
        else if (chipResponse == DONE)
        {
            cout << "transfer complete\n";
        }
        else if (chipResponse == 0x07)
        {
            cout << "chunk received\n";
            ackReceived = true;
        }
        else if (chipResponse == ACK)
        {
            cout << "256bytes Saved\n";
            ackReceived = true;
        }
    }
}

int main()
{
    // lets get the size of the audio file
    ifstream audioFile;
    streampos size;
    char * audioFileMemBlock;
    char * audioBufferForTransfer;

    const char* portname = "/dev/cu.wchusbserial14530";
    int serialPort = openSerialPort(portname);
    if (serialPort < 0)
    {
        cout << "could not open serial port";
        return 1;
    }
    
    if (!configureSerialPort(serialPort, B115200)) {
        closeSerialPort(serialPort);
        return 1;
    }

    // flush any floating data in the I/O buffers
    tcflush(serialPort, TCIOFLUSH);
    sleep(2);

    audioFile.open("trainbeep.wav", ios::binary | ios::ate);
    if (audioFile.is_open())
    {
        size = audioFile.tellg();
        
        cout << "The size of your file is: ";
        cout << size << "\n";

        // move get position pointer back to front of file
        audioFile.seekg(0, ios::beg);

        // load the audio file into memory
        audioFileMemBlock = new char [size];
        audioFile.read(audioFileMemBlock, size);

        audioBufferForTransfer = new char [64];

        // iterate over the memblock array and send the file contents byte by byte
        int counter = 0;
        int bytesLeftInChunk = 0;
        int bytesRemaining = static_cast<int>(size);
        uint8_t bytesBeingSent = 64;
        
        while (counter < size)
        {
            audioBufferForTransfer[(counter % 64)] = audioFileMemBlock[counter];

            counter++;
            bytesRemaining--;

            bytesLeftInChunk = 64 - (counter % 64);
            
            if (bytesLeftInChunk == 64 || bytesRemaining == 0)
            {
                // figure out how many bytes are being sent
                if (bytesRemaining == 0 && (counter % 64) != 0)
                {
                    bytesBeingSent = counter % 64;
                }
                else
                {
                    bytesBeingSent = 64;
                }

                // send data to chip along with info about how many bytes it should read from the buffer
                uint8_t sizeBuffer[1] = { bytesBeingSent };
                if (writeToSerialPort(serialPort, (const char*)sizeBuffer, 1) < 0)
                {
                    cout << "problem sending buffer size";
                    closeSerialPort(serialPort);
                    audioFile.close();
                    return 1;
                }
                cout << "Sent size: " << bytesBeingSent << "\n";
                responseFromChip(serialPort);

                if (writeToSerialPort(serialPort, audioBufferForTransfer, bytesBeingSent) < 0)
                {
                    cout << "problem sending audio buffer";
                    closeSerialPort(serialPort);
                    audioFile.close();
                    return 1;
                }    
                cout << "Sent 64bytes\n";
                responseFromChip(serialPort);
            }
        }
        
        audioFile.close();
        delete[] audioFileMemBlock;
        delete[] audioBufferForTransfer;
    }

    closeSerialPort(serialPort);
    return 0;
}