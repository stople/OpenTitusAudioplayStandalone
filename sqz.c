/*   
 * Copyright (C) 2008 Eirik Stople
 *
 * OpenTitus is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 3  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include "sqz.h"
#include "tituserror.h"

static int lzw_decode(unsigned char *input, int in_len, unsigned char *output, int out_len);
static int huffman_decode(unsigned char *input, int in_len, unsigned char *output, int out_len);

/*
int SQZ_get_length(char *inputfile) {
    int i = 0;
    char b1;
    char b2;
    char b3;
    char b4;
    int out_len = 0;
    FILE *ifp;

    ifp = fopen(inputfile, "rb");

    if (ifp == NULL) {
        fprintf(stderr, "Can't open input file: %s!\n", inputfile);
        return(-1);
    }

    if (fscanf(ifp, "%c%c%c%c", &b1, &b2, &b3, &b4) == EOF) {
        fprintf(stderr, "File too short: %s!\n", inputfile);
        fclose (ifp);
        return(-2);
    }

    out_len = (b1 & 0x0F);
    out_len <<= 8;
    out_len += (b4 & 0x000000FF);
    out_len <<= 8;
    out_len += (b3 & 0x000000FF);

    fclose (ifp);
    if (out_len == 0)
        out_len = -10;
    return out_len;
}
*/

int unSQZ(unsigned char *input, int in_len, unsigned char **output) {
    int i = 0;
    char comp_type;
    unsigned char *inbuffer;
    char b1;
    char b3;
    char b4;
    int out_len = 0;

    b1 = (char)input[0];
    comp_type = (char)input[1];
    b3 = (char)input[2];
    b4 = (char)input[3];

/*
    if (sscanf(input, "%c%c%c%c", &b1, &comp_type, &b3, &b4) == EOF) {
        fprintf(stderr, "Error: File too short!\n");
        return (TITUS_ERROR_INVALID_FILE);
    }
*/

    out_len = (b1 & 0x0F);
    out_len <<= 8;
    out_len += (b4 & 0x000000FF);
    out_len <<= 8;
    out_len += (b3 & 0x000000FF);

    if (out_len == 0) {
        fprintf(stderr, "Error: Invalid file!\n");
        return (TITUS_ERROR_INVALID_FILE);
    }

    *output = (unsigned char *)malloc(sizeof(unsigned char)*out_len);
    if (*output == NULL) {
        fprintf(stderr, "Error: Not enough memory (output buffer) to decompress file, needs %lu bytes!\n", sizeof(unsigned char) * out_len);
        return (TITUS_ERROR_NOT_ENOUGH_MEMORY);
    }

    if (comp_type == 0x10) {
        i = lzw_decode(input + 4, in_len, *output, out_len);
        if (i == TITUS_ERROR_NOT_ENOUGH_MEMORY)
            fprintf(stderr, "Error: Not enough memory (dictionary) to decompress file, needs %lu bytes!\n", LZW_MAX_TABLE * sizeof(unsigned int) + LZW_MAX_TABLE * sizeof(unsigned char));
        if (i == TITUS_ERROR_INVALID_FILE)
            fprintf(stderr, "Error: Dictionary overflow during decompression!\n");
    } else {
        i = huffman_decode(input + 4, in_len, *output, out_len);
    }
    if (i == 0)
        i = out_len;
    return i;
}

static int lzw_decode(unsigned char *input, int in_len, unsigned char *output, int out_len) {
    unsigned char nbit = 9;
    unsigned char bitadd = 0;
    unsigned int i = 0;
    unsigned int k_pos = 0;
    unsigned int k;
    unsigned int tmp_k;
    unsigned int w;
    unsigned int out_pos = 0;
    char addtodict = 0;
    unsigned int *dict_prefix;
    unsigned char *dict_character;
    unsigned int dict_length = 0;
    unsigned char dict_stack[LZW_MAX_TABLE];

    dict_prefix = (unsigned int *)malloc(LZW_MAX_TABLE * sizeof(unsigned int));
    dict_character = (unsigned char *)malloc(LZW_MAX_TABLE * sizeof(unsigned char));

    if (dict_prefix == NULL || dict_character == NULL) {
        if (dict_prefix != NULL)
            free (dict_prefix);
        if (dict_character != NULL)
            free (dict_character);
        return (TITUS_ERROR_NOT_ENOUGH_MEMORY);
    }

    while ((k_pos < in_len) && (out_pos < out_len)) {

        k = 0;
        i = 0;
        while (i < 4) {
            k <<= 8;
            if ((k_pos + i < in_len) && (i * 8 < bitadd + nbit))
                k += input[k_pos + i];
            i++;
        }
        k <<= bitadd;
        k >>= sizeof(int) * 8 - nbit;

        bitadd += nbit;
        while (bitadd > 8) {
            bitadd -= 8;
            k_pos++;
        }
        if (k == LZW_CLEAR_CODE) {
            nbit = 9;
            dict_length = 0;
            addtodict = 0;
        } else if (k != LZW_END_CODE) {
            if (k > 255 && k < LZW_FIRST + dict_length) {
                i = 0;
                tmp_k = k;
                while (tmp_k >= LZW_FIRST) {
                    dict_stack[i] = dict_character[tmp_k - LZW_FIRST];
                    tmp_k = dict_prefix[tmp_k - LZW_FIRST];
                    if (i++ >= LZW_MAX_TABLE)
                    {
                        free (dict_prefix);
                        free (dict_character);
                        return (TITUS_ERROR_INVALID_FILE);
                    }
                }
                dict_stack[i++] = tmp_k;
                tmp_k = i - 1;
                while (--i > 0)
                    output[out_pos++] = dict_stack[i];

                output[out_pos++] = dict_stack[0];

                dict_stack[0] = dict_stack[tmp_k];

            } else if (k > 255 && k >= LZW_FIRST + dict_length) {
                i = 1;
                tmp_k = w;
                while (tmp_k >= LZW_FIRST) {
                    dict_stack[i] = dict_character[tmp_k - LZW_FIRST];
                    tmp_k = dict_prefix[tmp_k - LZW_FIRST];
                    if (i++ >= LZW_MAX_TABLE)
                    {
                        free (dict_prefix);
                        free (dict_character);
                        return (TITUS_ERROR_INVALID_FILE);
                    }
                }
                dict_stack[i++] = tmp_k;

                if (dict_length > 0)
                    tmp_k = dict_character[dict_length - 1];
                else
                    tmp_k = w;

                dict_stack[0] = tmp_k;
                tmp_k = i - 1;
                while (--i > 0)
                    output[out_pos++] = dict_stack[i];

                output[out_pos++] = dict_stack[0];
                dict_stack[0] = dict_stack[tmp_k];
            } else {
                output[out_pos++] = k;
                dict_stack[0] = k;
            }
            if (addtodict && (LZW_FIRST + dict_length < LZW_MAX_TABLE)) {
                dict_character[dict_length] = dict_stack[0];
                dict_prefix[dict_length] = w;
                dict_length++;
            }

            w = k;
            addtodict = 1;
        }
        if ((LZW_FIRST + dict_length == ((unsigned int)(1)<<nbit)) && (nbit < 12))
            nbit++;
    }

    free (dict_prefix);
    free (dict_character);

    return out_pos;
}

static int huffman_decode(unsigned char *input, int in_len, unsigned char *output, int out_len) {
    unsigned short treesize = (((unsigned short)input[1]) << 8) + (unsigned short)input[0];
    unsigned short bintree;
    unsigned short node = 0;
    unsigned char nodeL;
    int state = 0;
    unsigned short count;
    unsigned int i;
    unsigned short j;
    unsigned char bit;
    unsigned char last;
    unsigned int out_pos = 0;

    for (i = 2 + treesize; i < in_len; i++) {
        for (bit = 128; bit >= 1; bit >>= 1) {
            if (input[i] & bit)
                node++;
            bintree = (((unsigned short)input[3 + node * 2]) << 8) + (unsigned short)input[2 + node * 2];
            if (bintree <= 0x7FFF)
                node = bintree >> 1;
            else {
                node = bintree & 0x7FFF;
                nodeL = (unsigned char)(node & 0x00FF);
                if (state == 0) {
                    if (node < 0x100) {
                        last = nodeL;
                        output[out_pos++] = last;
                    } else if (nodeL == 0) {
                        state = 1;
                    } else if (nodeL == 1) {
                        state = 2;
                    } else {
                        for (j = 1; j <= (unsigned short)nodeL; j++)
                            output[out_pos++] = last;
                    }
                } else if (state == 1) {
                    for (j = 1; j <= node; j++)
                        output[out_pos++] = last;
                    state = 0;
                } else if (state == 2) {
                    count = 256 * (unsigned short)nodeL;
                    state = 3;
                } else if (state == 3) {
                    count += (unsigned short)nodeL;
                    for (j = 1; j <= count; j++)
                        output[out_pos++] = last;
                    state = 0;
                }
                node = 0;
            }
        }
    }
    return out_pos;
}
