#ifndef _SD_RAW_ERR_H__

#define _SD_RAW_ERR_H__

#define SDR_ERR_BADRESPONSE  1
#define SDR_ERR_COMMS        2
#define SDR_ERR_CRC          3
#define SDR_ERR_EINVAL       4
#define SDR_ERR_LOCKED       5
#define SDR_ERR_NOCARD       6
#define SDR_ERR_PATTERN      7
#define SDR_ERR_VOLTAGE      8

#ifdef __cplusplus
extern "C"
{
#endif

extern uint8_t sd_errno;

#ifdef __cplusplus
}
#endif

#endif
