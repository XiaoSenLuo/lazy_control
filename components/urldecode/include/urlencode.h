
#ifndef _URL_ENCODE_H_
#define _URL_ENCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

/** url encode
 *  parameters:
 *  	src: the source string to encode
 *  	src_len: source string length
 *  	dest: store dest string
 *  	dest_len: store the dest string length
 *  return: error no, 0 success, != 0 fail
*/
char *urlencode(const char *src, const int src_len, char *dest, int *dest_len);


#ifdef __cplusplus
}
#endif

#endif