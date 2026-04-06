#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include "main.h"

// Initialize the dedicated diagnostic RTOS task
void Diag_InitTask(void);

// Triggers from the Command Parser
void Diag_StartSequence(void);
void Diag_ProcessInput(char response);

// Existing benchmark tools
void Diag_RunSPIBenchmark(void);
void Diag_RunSPIBulk(void);
void Diag_RunSDCardTest(void);
void Diag_RunRawSpeedTest(void);
void Diag_RunRawSectorTest(void);
void Diag_RunUSBReadTest(void);
void Diag_RunI2CBenchmark(void);


#endif // DIAGNOSTICS_H
