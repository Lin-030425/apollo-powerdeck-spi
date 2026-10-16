#ifndef SPI_LINK_CONFIG_H
#define SPI_LINK_CONFIG_H

/* Keep SPI_FRAME_SIZE_BYTES equal to SPI_TEST_FRAME_SIZE in the Apollo
 * project. The Apollo master controls SCK and the frame period.
 */
#define SPI_FRAME_SIZE_BYTES   14900U
#define SPI_REPORT_INTERVAL_US 1000000LL
#define SPI_RX_QUEUE_DEPTH     3U

#define SPI_PIN_SCLK 6
#define SPI_PIN_MOSI 7
#define SPI_PIN_MISO (-1)
#define SPI_PIN_CS   4

#endif /* SPI_LINK_CONFIG_H */
