#ifndef FRONTEND_API_H
#define FRONTEND_API_H

#include <stdint.h>
#include <stdbool.h>

void Frontend_Init(void);
void Frontend_ProcessBlock(const uint8_t* data, uint16_t len);
void Frontend_RunSDCardTest(void);
void Frontend_CheckTimeout(void);

#endif // FRONTEND_API_H
