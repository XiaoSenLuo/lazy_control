
#ifndef _URL_DECODE_H_
#define _URL_DECODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define IS_UPPER_HEX(ch) ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))
#define IS_HEX_CHAR(ch)  (IS_UPPER_HEX(ch) || (ch >= 'a' && ch <= 'f'))

/** url decode, terminated with \0
 *  parameters:
 *  	src: the source string to decode
 *  	src_len: source string length
 *  	dest: store dest string
 *  	dest_len: store the dest string length
 *  return: error no, 0 success, != 0 fail
*/
char *urldecode(const char *src, const int src_len, char *dest, int *dest_len);

/** url decode, no terminate with \0
 *  parameters:
 *  	src: the source string to decode
 *  	src_len: source string length
 *  	dest: store dest string
 *  	dest_len: store the dest string length
 *  return: error no, 0 success, != 0 fail
*/
char *urldecode_ex(const char *src, const int src_len, char *dest, int *dest_len);


#ifdef __cplusplus
}
#endif

#endif