#ifndef SPI_RECORDER_H
#define SPI_RECORDER_H

/* Takes over USART2, SPI1, SPI2 and port B, then waits for 'c' on the COM port. Never returns. */
void spi_recorder_run(void);

#endif /* SPI_RECORDER_H */
