#include "mbed.h"
#include "rtos.h"
#include "string.h"
#include <stdio.h>
#include <ctype.h>

#define SAMPLING 1
#define INPUT 2
#define OUTPUT 3
#define BUFFERSIZE 120
#define INPUTLENGTH 15
#define LOCKED 1
#define UNLOCKED 0

float samplingRate = 0.1; //seconds

//Serial Interface
Serial pc(USBTX, USBRX);

//Leds
DigitalOut redLED(D7);
DigitalOut yellowLED(D6);
DigitalOut greenLED(D5);

//Sampling input
AnalogIn POT_ADC_In(A0);
Ticker samplesTicker;
bool sampling = false;

//Threads
Thread* samplingThread;
Thread* inputThread;
Thread* outputThread;

//Mutex
Mutex * bufferMutex;

//circular buffer
int bufferLength = 0;
float buffer[BUFFERSIZE];
float * validStart = buffer;
float * validEnd = buffer;

//running average
float sampleAverage = 0.0;

//buffers for input and pointer for atoi
char inputBuffer[INPUTLENGTH];
char dataOperation[INPUTLENGTH];
char dataOperand[INPUTLENGTH];
char * endPtr;

void doSampling()
{
	while(1)
	{
		Thread::signal_wait(SAMPLING);
		
		greenLED = !greenLED;
		
		float newSample = POT_ADC_In;
		
		//prevents conflicts with delete routine
		bufferMutex->lock();
		
		//if the buffer is full or empty, move the start pointer forwards
		if(bufferLength == BUFFERSIZE || bufferLength == 0)
		{
			validStart++;
			//check pointer isnt beyond buffer range
			if(validStart >= buffer + BUFFERSIZE)
			{
				validStart = buffer;
			}
		}
		//move the end pointer forwards
		validEnd++;
		//check pointer isnt beyond buffer range
		if(validEnd >= buffer + BUFFERSIZE)
		{
			validEnd = buffer;
		}
		//insert the data and increase buffer length
		*validEnd = newSample;
		//calculate running average
		float newSampleAverage = sampleAverage * bufferLength;
		if(bufferLength != BUFFERSIZE)
		{
			bufferLength++;
		}
		else
		{
			//remove item to be replaced from average
			newSampleAverage -= *validStart;
		}
		newSampleAverage += newSample;
		sampleAverage = newSampleAverage / bufferLength;
		
		bufferMutex->unlock();
		
		samplingThread->signal_clr(SAMPLING);
	}
}

void printSamples(int n)
{
	if(n > bufferLength || n < 0)
	{
		n = bufferLength;
		printf("You specified a number higher than the current length of the buffer. There are currently only %d samples in the buffer; %d samples will be printed\r\n", n, n);
	}
	//start of valid data is oldest
	float * element = validStart;
	for(int i = 0; i < n; i++)
	{
		//check pointer isnt beyond buffer range
		if(element >= buffer + BUFFERSIZE)
		{
				element = buffer;
		}
		printf("Sample %d from oldest: %f\r\n", i+1, *element);
		element++;
	}
}

void deleteSamples(int n)
{
	//lock to stop conflicts with adding new samples
	bufferMutex->lock();
	
	if(n > bufferLength || n < 0)
	{
		n = bufferLength;
		printf("You specified a number higher than the current length of the buffer. There are currently only %d samples in the buffer; %d samples will be deleted\r\n", n, n);
	}
	//delete the samples
	//start of valid data is oldest. move pointer to delete, if it is beyond the buffer, - buffersize.
	validStart += n;
	if(validStart > buffer + BUFFERSIZE)
		validStart -= BUFFERSIZE;
	
	bufferLength -= n;
	//if buffer is empty, start and end must be equal
	if(bufferLength == 0)
	{
		validEnd = validStart;
	}
	
	//recalculate average
	float total = 0;
	float * element = validStart;
	for(int i = 0; i < bufferLength; i++)
	{
		element++;
		if(element > buffer + BUFFERSIZE) //if beyond end, set to start
			element = buffer;
		total += *element;
	}
	sampleAverage = total / bufferLength;
	
	bufferMutex->unlock();
	
	printf("%d samples from oldest were deleted\r\n", n);
}

void samplingISR()
{
	samplingThread->signal_set(SAMPLING);
}

void enableSampling()
{
	sampling = true;
	redLED = 0;
	samplesTicker.attach(samplingISR, samplingRate);
}

void disableSampling()
{
	samplesTicker.detach();
	sampling = false;
	redLED = 1;
	greenLED = 0;
}

void processInput()
{
	while(1)
	{
		Thread::signal_wait(INPUT);
		//clear buffers to 0
		memset(inputBuffer, 0, INPUTLENGTH);
		memset(dataOperation, 0, INPUTLENGTH);
		memset(dataOperand, 0, INPUTLENGTH);
		//get the input and put in inputBuffer
		char thisChar;
		for(int i = 0; i < INPUTLENGTH; i++)
		{
			//Get a character, check if it is an enter or backspace
			thisChar = pc.getc();
			if((thisChar == 0x8 || thisChar == 0x7F) && i > 0)
			{
				i-=2;
				inputBuffer[i+1] = 0;
			}
			else if(thisChar == 0xD)
			{
				i = INPUTLENGTH;
			}
			else
			{
				inputBuffer[i] = thisChar;
			}
		}
		//trim the command to the operator and operand
		int inputCounter = 0;
		for(int i = 0; i < INPUTLENGTH; i++)
		{
			thisChar = inputBuffer[inputCounter];
			if(thisChar == 0x20)
				i = INPUTLENGTH;
			else
				dataOperation[i] = thisChar;
			inputCounter++;
		}
		for(int i = 0; i < INPUTLENGTH - inputCounter; i++)
		{
			thisChar = inputBuffer[inputCounter];
			if(thisChar != 0x20)
				dataOperand[i] = thisChar;
			inputCounter++;
		}
		outputThread->signal_set(OUTPUT);
		
		inputThread->signal_clr(INPUT);
	}
}

void terminalOut() {
	pc.printf("\r\nValid commands you can use are: print (n), delete(n), sampling(on/off)\r\n");
	while(1)
	{
		printf("-> ");
		inputThread->signal_set(INPUT);
		Thread::signal_wait(OUTPUT);
		printf("\r\n");
		
		//find the command
		if (strcmp(dataOperation, "print") == 0)
		{
			//check operand is an int
			int opdInt = strtol(dataOperand, &endPtr, 10);
			if(opdInt > 0 && opdInt <= BUFFERSIZE)
			{
				printSamples(opdInt);
			}
			else
			{
				printf("Invalid input - usage: print(n) where %d >= n > 0\r\n", BUFFERSIZE);
			}
		}
		else if (strcmp(dataOperation, "delete") == 0)
		{
			//check operand is an int
			int opdInt = strtol(dataOperand, &endPtr, 10);
			if(opdInt > 0 && opdInt <= BUFFERSIZE)
			{
				printf("Are you sure you want to delete %d records? (y/n)\r\n-> ", opdInt);
				if(pc.getc() == 'y')
				{
					printf("\r\n");
					deleteSamples(opdInt);
				}
				else
					printf("\r\nOperation cancelled\r\n");
			}
			else
			{
				printf("Invalid input - usage: delete(n) where %d >= n > 0\r\n", BUFFERSIZE);
			}
		}
		else if (strcmp(dataOperation, "sampling") == 0)
		{
			//check operand is on/off
			if (strcmp(dataOperand, "on") == 0)
			{
				if(!sampling)
				{
					enableSampling();
					printf("Sampling was turned on\r\n");
				}
				else
					printf("Sampling was already on - no change has been made\r\n");
			}
			else if (strcmp(dataOperand, "off") == 0)
			{
				if(sampling)
				{
					disableSampling();
					printf("Sampling was turned off\r\n");
				}
				else
					printf("Sampling was already off - no change has been made\r\n");
			}
			else
				printf("A valid operand was not entered. Valid operands are: on, off\r\n");
		}
		else
		{
			printf("A valid command was not entered. Valid commands are: print (n), delete(n), sampling(on/off)\r\n");
		}
		
		outputThread->signal_clr(OUTPUT);
	}
}

int main() {
	
	//Set baud rate to 9600
  pc.baud(9600);
	
	bufferMutex = new Mutex();
	
	samplingThread = new Thread(doSampling);
	outputThread = new Thread(terminalOut);
	inputThread = new Thread(processInput);
	
	enableSampling();
	
	while(1)
	{
		Thread::wait(osWaitForever);
	}

	return 0;

}




