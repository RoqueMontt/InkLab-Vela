/**
 * @file diagnostics.h
 * @brief Benchmark and physical diagnostic test suites.
 */
#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

/** @brief Polls I2C sensors continuously to calculate bandwidth limits. */
void Diag_RunI2CBenchmark(void);

/** @brief Mounts, writes, and reads a small file to verify FS integrity. */
void Diag_RunSDCardTest(void);

/** @brief Writes a 1MB file to test pure Zephyr FS write speed. */
void Diag_RunRawSpeedTest(void);

/** @brief Benchmarks physical un-buffered SPI read limits. */
void Diag_RunRawSectorTest(void);

/** @brief Reads a large file and relays it over USB to test CDC-ACM pipe limits. */
void Diag_RunUSBReadTest(void);

#endif /* DIAGNOSTICS_H */