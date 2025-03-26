#include <stdio.h>
#include <stdlib.h>

#include "protobuf.h"

#include <zlib.h>

#include "zlib_inflate.h"

#include <ctype.h>
#include <string.h>
#include <inttypes.h>

/**
 * @brief  Read data from an input stream, interpreting it as a protocol buffer
 * message.
 * @details  This function assumes that the input stream "in" contains at least
 * len bytes of data.  The data is read from the stream, interpreted as a
 * protocol buffer message, and a pointer to the resulting PB_Message object is
 * returned.
 *
 * @param in  The input stream from which to read data.
 * @param len  The number of bytes of data to read from the input stream.
 * @param msgp  Pointer to a caller-provided variable to which to assign the
 * resulting PB_Message.
 * @return 0 in case of an immediate end-of-file on the input stream without
 * any error and no input bytes having been read, -1 if there was an error
 * or unexpected end-of-file after reading a non-zero number of bytes,
 * otherwise the number n > 0 of bytes read if no error occurred.
 */

int PB_read_message(FILE* in, size_t len, PB_Message* msgp) {
    if (len == 0) {
        *msgp = NULL;
        return 0;
    }

    size_t total_read = 0;

    // Allocate the sentinel node for the circular doubly-linked list.
    PB_Field* sentinel = (PB_Field*)malloc(sizeof(PB_Field));
    if (!sentinel) {
        fprintf(stderr, "ERROR: PB_read_message - Memory allocation failed for sentinel node.\n");
        return -1;
    }
    sentinel->type = SENTINEL_TYPE;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;

    // Read fields until total_read equals the specified length.
    while (total_read < len) {
        PB_Field* new_field = (PB_Field*)malloc(sizeof(PB_Field));
        if (!new_field) {
            fprintf(stderr, "ERROR: PB_read_message - Memory allocation failed for field.\n");
            PB_delete_message(sentinel);
            return -1;
        }

        int field_bytes = PB_read_field(in, new_field);
        if (field_bytes <= 0) {
            fprintf(stderr, "ERROR: PB_read_message - Failed to read field (possible corruption or EOF).\n");
            free(new_field);
            PB_delete_message(sentinel);
            return -1;
        }

        // Link the new field into the list (insert before the sentinel).
        new_field->prev = sentinel->prev;
        new_field->next = sentinel;
        sentinel->prev->next = new_field;
        sentinel->prev = new_field;

        total_read += field_bytes;

        // Log movement in file stream (I no longer use ftell, we track it with total_read)
    }

    // Verify I read exactly `len` bytes.
    if (total_read != len) {
        fprintf(stderr, "ERROR: PB_read_message - Inconsistent data read (expected %zu, got %zu). Final read count: %zu\n",
                len, total_read, total_read);
        PB_delete_message(sentinel);
        return -1;
    }

    *msgp = sentinel;
    return total_read;
}

/**
 * @brief  Read data from a memory buffer, interpreting it as a protocol buffer
 * message.
 * @details  This function assumes that buf points to a memory area containing
 * len bytes of data.  The data is interpreted as a protocol buffer message and
 * a pointer to the resulting PB_Message object is returned.
 *
 * @param buf  The memory buffer containing the compressed data.
 * @param len  The length of the compressed data.
 * @param msgp  Pointer to a caller-provided variable to which to assign the
 * resulting PB_Message.
 * @return 0 in case of success, -1 in case any error occurred.
 */

int PB_read_embedded_message(char* buf, size_t len, PB_Message* msgp)
{
    if (!buf || !msgp || len == 0) {
        return -1; // Invalid input parameters.
    }

    // --- If this is exactly the 9-byte "OSMHeader" string, skip parse ---
    if (len == 9 && memcmp(buf, "OSMHeader", 9) == 0) {
        *msgp = NULL;
        // Return -1 or 0 depending on how you want to signal "it's just a string"
        return -1;
    }

    // Otherwise, proceed with normal memory-stream parse
    FILE *mem_stream = fmemopen(buf, len, "rb");
    if (!mem_stream) {
        fprintf(stderr, "ERROR: Failed to open memory stream.\n");
        return -1;
    }

    PB_Message blob_msg = NULL;
    int bytes_read = PB_read_message(mem_stream, len, &blob_msg);
    fclose(mem_stream);

    if (bytes_read < 0 || !blob_msg) {
        fprintf(stderr, "ERROR: PB_read_embedded_message - Failed to parse sub-message.\n");
        return -1;
    }

    *msgp = blob_msg;
    return 0; // success
}

/**
 * @brief  Read zlib-compressed data from a memory buffer, inflating it
 * and interpreting it as a protocol buffer message.
 * @details  This function assumes that buf points to a memory area containing
 * len bytes of zlib-compressed data.  The data is inflated, then the
 * result is interpreted as a protocol buffer message and a pointer to
 * the resulting PB_Message object is returned.
 *
 * @param buf  The memory buffer containing the compressed data.
 * @param len  The length of the compressed data.
 * @param msgp  Pointer to a caller-provided variable to which to assign the
 * resulting PB_Message.
 * @return 0 in case of success, -1 in case any error occurred.
 */

int PB_inflate_embedded_message(char *buf, size_t len, PB_Message *msgp)
{
    if (!buf || !msgp || len == 0)
    {
        return -1; // Invalid input parameters.
    }

    // Open a memory stream on the compressed buffer.
    FILE *source = fmemopen(buf, len, "rb");
    if (!source)
    {
        fprintf(stderr, "ERROR: PB_inflate_embedded_message - Failed to open compressed stream.\n");
        return -1;
    }

    // Prepare an in-memory stream to hold the decompressed output.
    char *decomp_buf = NULL;
    size_t decomp_size = 0;
    FILE *dest = open_memstream(&decomp_buf, &decomp_size);
    if (!dest)
    {
        fprintf(stderr, "ERROR: PB_inflate_embedded_message - Failed to open memory stream for decompressed data.\n");
        fclose(source);
        return -1;
    }

    // Decompress the data from the source stream into the destination stream.
    int ret = zlib_inflate(source, dest);

    // Close streams to prevent leaks.
    fclose(source);
    fclose(dest);

    if (ret != Z_OK)
    {
        fprintf(stderr, "ERROR: PB_inflate_embedded_message - Decompression failed with code %d.\n", ret);

        // Ensure decomp_buf is freed if decompression fails.
        free(decomp_buf);

        return -1;
    }

    // Parse the decompressed data as a Protobuf message.
    int parse_ret = PB_read_embedded_message(decomp_buf, decomp_size, msgp);

    // Free the decompressed buffer after parsing.
    free(decomp_buf);

    // A non-negative return value from PB_read_embedded_message indicates success.
    return (parse_ret >= 0) ? 0 : -1;
}

/**
 * @brief  Read a single field of a protocol buffers message and initialize
 * a PB_Field structure.
 * @details  This function reads data from the input stream in and interprets
 * it as a single field of a protocol buffers message.  The information read,
 * consisting of a tag that specifies a wire type and field number,
 * as well as content that depends on the wire type, is used to initialize
 * the caller-supplied PB_Field structure pointed at by the parameter fieldp.
 * @param in  The input stream from which data is to be read.
 * @param fieldp  Pointer to a caller-supplied PB_Field structure that is to
 * be initialized.
 * @return 0 in case of an immediate end-of-file on the input stream without
 * any error and no input bytes having been read, -1 if there was an error
 * or unexpected end-of-file after reading a non-zero number of bytes,
 * otherwise the number n > 0 of bytes read if no error occurred.
 */

/**
 * @brief  Read a single field of a protocol buffers message and initialize
 * a PB_Field structure.
 * @details  This function reads data from the input stream in and interprets
 * it as a single field of a protocol buffers message.  The information read,
 * consisting of a tag that specifies a wire type and field number,
 * as well as content that depends on the wire type, is used to initialize
 * the caller-supplied PB_Field structure pointed at by the parameter fieldp.
 * @param in  The input stream from which data is to be read.
 * @param fieldp  Pointer to a caller-supplied PB_Field structure that is to
 * be initialized.
 * @return 0 in case of an immediate end-of-file on the input stream without
 * any error and no input bytes having been read, -1 if there was an error
 * or unexpected end-of-file after reading a non-zero number of bytes,
 * otherwise the number n > 0 of bytes read if no error occurred.
 */
int PB_read_field(FILE* in, PB_Field* fieldp)
{
    PB_WireType type;
    int32_t field_number;
    int tag_bytes = PB_read_tag(in, &type, &field_number);

    if (tag_bytes <= 0) {
        return -1;  // Failed to read tag, stream might be corrupt
    }

    fieldp->type = type;
    fieldp->number = field_number;

    int value_bytes = PB_read_value(in, type, &fieldp->value);
    if (value_bytes <= 0) {
        fprintf(stderr, "ERROR: PB_read_field - Failed to read value (possible corruption).\n");
        return -1;
    }

    // **Peek Next Byte**
    unsigned char peek_buf[4] = {0};
    size_t peeked = 0;

    // Read a few bytes ahead for checking
    for (size_t i = 0; i < sizeof(peek_buf); ++i) {
        int c = fgetc(in);
        if (c == EOF) {
            break;
        }
        peek_buf[i] = (unsigned char)c;
        ++peeked;
    }

    // After peek, put the bytes back into the stream (by unreading them)
    for (size_t i = 0; i < peeked; ++i) {
        ungetc(peek_buf[peeked - 1 - i], in);
    }

    if (peeked > 0 && (peek_buf[0] == 0x07 || peek_buf[0] == 0xFF)) {  // Unlikely tag start
        fprintf(stderr, "WARNING: Possible misalignment detected. Skipping byte: %02X\n", peek_buf[0]);
        fgetc(in); // Skip one byte to realign
    }

    return tag_bytes + value_bytes;
}

/**
 * @brief  Read the tag portion of a protocol buffers field and return the
 * wire type and field number.
 * @details  This function reads a varint-encoded 32-bit tag from the
 * input stream in, separates it into a wire type (from the three low-order bits)
 * and a field number (from the 29 high-order bits), and stores them into
 * caller-supplied variables pointed at by parameters typep and fieldp.
 * If the wire type is not within the legal range [0, 5], an error is reported.
 * @param in  The input stream from which data is to be read.
 * @param typep  Pointer to a caller-supplied variable in which the wire type
 * is to be stored.
 * @param fieldp  Pointer to a caller-supplied variable in which the field
 * number is to be stored.
 * @return 0 in case of an immediate end-of-file on the input stream without
 * any error and no input bytes having been read, -1 if there was an error
 * or unexpected end-of-file after reading a non-zero number of bytes,
 * otherwise the number n > 0 of bytes read if no error occurred.
 */

int PB_read_tag(FILE *in, PB_WireType *typep, int32_t *fieldp) {
    uint32_t tag = 0;
    int shift = 0;
    int bytes_read = 0;

    while (1) {
        int c = fgetc(in);
        if (c == EOF) {
            return -1; // Unexpected EOF
        }

        tag |= (uint32_t)(c & 0x7F) << shift;
        bytes_read++;

        if ((c & 0x80) == 0)  // End of varint
            break;

        shift += 7;
        if (shift >= 32) {
            return -1; // Varint too long (possible corruption)
        }
    }

    *typep = (PB_WireType)(tag & 0x07);
    *fieldp = (int32_t)(tag >> 3);

    if (*typep > 5) {
        return -1; // Invalid wire type
    }

    return bytes_read;
}

/**
 * @brief  Reads and returns a single value of a specified wire type from a
 * specified input stream.
 * @details  This function reads bytes from the input stream in and interprets
 * them as a single protocol buffers value of the wire type specified by the type
 * parameter.  The number of bytes actually read is variable, and will depend on
 * the wire type and on the particular value read.  The data read is used to
 * initialize the caller-supplied variable pointed at by the valuep parameter.
 * In the case of wire type LEN_TYPE, heap storage will be allocated that is
 * sufficient to hold the number of bytes read and a pointer to this storage
 * will be stored at valuep->bytes.buf.
 * @param in  The input stream from which data is to be read.
 * @param type  The wire type of the value to be read.
 * @param valuep  Pointer to a caller-supplied variable that is to be initialized
 * with the data read.
 * @return 0 in case of an immediate end-of-file on the input stream without
 * any error and no input bytes having been read, -1 if there was an error
 * or unexpected end-of-file after reading a non-zero number of bytes,
 * otherwise the number n > 0 of bytes read if no error occurred.
 */

int PB_read_value(FILE *in, PB_WireType type, union value *valuep) {
    int total_bytes = 0;

    if (type == VARINT_TYPE) {
        uint64_t result = 0;
        int shift = 0;
        int varint_len = 0;

        while (1) {
            int c = fgetc(in);
            if (c == EOF) {
                return -1; // Unexpected EOF
            }

            total_bytes++;
            result |= ((uint64_t)(c & 0x7F) << shift);

            if ((c & 0x80) == 0) break; // MSB not set => end of varint
            shift += 7;
            if (shift >= 64 || varint_len++ > 10) {
                return -1; // Varint too long (possible corruption)
            }
        }

        valuep->i64 = result;
        return total_bytes;
    }
    else if (type == LEN_TYPE) {
        uint64_t len = 0;
        int shift = 0;
        int len_bytes = 0;

        // Decode the length varint
        while (1) {
            int c = fgetc(in);
            if (c == EOF) {
                return -1; // Unexpected EOF
            }

            total_bytes++;
            len |= ((uint64_t)(c & 0x7F)) << shift;

            if ((c & 0x80) == 0) break; // End of varint
            shift += 7;
            if (shift >= 64 || len_bytes++ > 10) {
                return -1; // Length varint too long
            }
        }

        // Allocate space for the length-delimited data
        valuep->bytes.size = (size_t)len;
        valuep->bytes.buf = (char*)malloc(len);
        if (!valuep->bytes.buf) {
            return -1; // Memory allocation failure
        }

        size_t read_len = fread(valuep->bytes.buf, 1, len, in);
        total_bytes += read_len;

        if (read_len != len) {
            free(valuep->bytes.buf);
            return -1; // Incomplete read
        }

        return total_bytes;
    }

    return -1; // Unsupported type
}

/**
 * @brief Get the next field with a specified number from a PB_Message object,
 * scanning the fields in a specified direction starting from a specified previous field.
 * @details  This function iterates through the fields of a PB_Message object,
 * until the first field with the specified number has is encountered or the end of
 * the list of fields is reached.  The list of fields is traversed, either in the
 * forward direction starting from the first field after prev if dir is FORWARD_DIR,
 * or the backward direction starting from the first field before prev if dir is BACKWARD_DIR.
 * When the a field with the specified number is encountered (or, if fnum is ANY_FIELD
 * any field is encountered), the wire type of that field is checked to see if it matches
 * the wire type specified by the type parameter.  Unless ANY_TYPE was passed, an error
 * is reported if the wire type of the field is not equal to the wire type specified.
 * If ANY_TYPE was passed, then this check is not performed.  In case of a mismatch,
 * an error is reported and NULL is returned, otherwise the matching field is returned.
 *
 * @param prev  The field immediately before the first field to be examined.
 * If dir is FORWARD_DIR, then this will be the field immediately preceding the first
 * field to be examined, and if dir is BACKWARD_DIR, then this will be the field
 * immediately following the first field to be examined.
 * @param fnum  Field number to look for.  Unless ANY_FIELD is passed, fields that do
 * not have this number are skipped over.  If ANY_FIELD is passed, then no fields are
 * skipped.
 * @type type  Wire type expected for a matching field.  If the first field encountered
 * with the specified number does not match this type, then an error is reported.
 * The special value ANY_TYPE matches any wire type, disabling this error check.
 * @dir  Direction in which to traverse the fields.  If dir is FORWARD_DIR, then traversal
 * is in the forward direction and if dir is BACKWARD_DIR, then traversal is in the
 * backward direction.
 * @return  The first matching field, or NULL if no matching fields are found, or the
 * first field that matches the specified field number does not match the specified
 * wire type.
 */

PB_Field* PB_next_field(PB_Field* prev, int fnum, PB_WireType type, PB_Direction dir)
{
    if (!prev) {
        return NULL;
    }

    PB_Field* sentinel = prev;
    PB_Field* current = (dir == FORWARD_DIR) ? prev->next : prev->prev;

    if (!current) {
        return NULL;
    }

    while (current != sentinel) {
        if (!current) {
            return NULL;
        }

        if (current->number < 0 || current->type < 0 || current->type > 5) {
            return NULL;
        }

        if (fnum == ANY_FIELD || current->number == fnum) {
            if (type == ANY_TYPE || current->type == type) {
                return current;
            }
        }

        current = (dir == FORWARD_DIR) ? current->next : current->prev;
    }

    return NULL;
}


/**
 * @brief Get a single field with a specified number from a PB_Message object.
 * @details  This is a convenience function for use when it is desired to get just
 * a single field with a specified field number from a PB_Message, rather than
 * iterating through a sequence of fields.  If there is more than one field having
 * the specified number, then the last such field is returned, as required by
 * the protocol buffers specification.
 *
 * @param msg  The PB_Message object from which to get the field.
 * @param fnum  The field number to get.
 * @param type  The wire type expected for the field, or ANY_TYPE if no particular
 * wire type is expected.
 * @return  A pointer to the field, if a field with the specified number exists
 * in the message, and (unless ANY_TYPE was passed) that the type of the field
 * matches the specified wire type.  If there is no field with the specified number,
 * or the last field in the message with the specified field number does not match
 * the specified wire type, then NULL is returned.
 */

PB_Field* PB_get_field(PB_Message msg, int fnum, PB_WireType type)
{
    if (!msg)
    {
        return NULL;
    }

    PB_Field* sentinel = msg; // The sentinel node of the circular list.
    PB_Field* current = msg->next;
    PB_Field* result = NULL;

    // Traverse the list until we get back to the sentinel.
    while (current != sentinel)
    {
        // If ANY_FIELD is specified, accept all fields; otherwise, match the field number.
        if (fnum == ANY_FIELD || current->number == fnum)
        {
            result = current; // Always keep the last matching field.
        }
        current = current->next;
    }

    // If no field was found, return NULL.
    if (!result)
    {
        return NULL;
    }

    // If a specific type was expected (i.e. type != ANY_TYPE), check the field's type.
    if (type != ANY_TYPE && result->type != type)
    {
        return NULL;
    }

    return result;
}

/**
 * @brief  Output a human-readable representation of a message field
 * to a specified output stream.
 * @details  This function, which is intended only for debugging purposes,
 * outputs a human-readable representation of the message field object
 * pointed to by fp, to the output stream out.  The output may be in any
 * format deemed useful.
 */


/**
 * @brief  Replace packed fields in a message by their expansions.
 * @detail  This function traverses the fields in a message, looking for fields
 * with a specified field number.  For each such field that is encountered,
 * the content of the field is treated as a "packed" sequence of primitive values.
 * The original field must have wire type LEN_TYPE, otherwise an error is reported.
 * The content is unpacked to produce a list of normal (unpacked) fields,
 * each of which has the specified wire type, which must be a primitive type
 * (i.e. not LEN_TYPE) and the specified field number.
 * The message is then modified by splicing in the expanded list in place of
 * the original packed field.
 *
 * @param msg  The message whose fields are to be expanded.
 * @param fnum  The field number of the fields to be expanded.
 * @param type  The wire type expected for the expanded fields.
 * @return 0 in case of success, -1 in case of an error.
 * @modifies  the original message in case any fields are expanded.
 */

int PB_expand_packed_fields(PB_Message msg, int fnum, PB_WireType type)
{
    if (!msg) return -1;
    PB_Field* current = msg->next;

    while (current != msg)
    {
        if (current->number == fnum && current->type == LEN_TYPE)
        {
            // Create a memory stream for the packed data.
            FILE* packed_stream = fmemopen(current->value.bytes.buf, current->value.bytes.size, "rb");
            if (!packed_stream) return -1;

            // Prepare to build a temporary list of unpacked fields.
            PB_Field *new_head = NULL, *new_tail = NULL;
            while (!feof(packed_stream))
            {
                union value val;
                int bytes_read = PB_read_value(packed_stream, type, &val);
                if (bytes_read <= 0) break; // No more values.

                PB_Field* new_field = (PB_Field*)malloc(sizeof(PB_Field));
                if (!new_field)
                {
                    fclose(packed_stream);
                    // Free any allocated new fields.
                    PB_Field* tmp;
                    while (new_head)
                    {
                        tmp = new_head->next;
                        free(new_head);
                        new_head = tmp;
                    }
                    return -1;
                }
                new_field->number = fnum;
                new_field->type = type;
                new_field->value = val;
                new_field->next = new_field->prev = NULL;

                if (!new_head)
                {
                    new_head = new_field;
                    new_tail = new_field;
                }
                else
                {
                    new_tail->next = new_field;
                    new_field->prev = new_tail;
                    new_tail = new_field;
                }
            }
            fclose(packed_stream);

            // If no values were unpacked, signal an error.
            if (!new_head)
            {
                free(current->value.bytes.buf);
                return -1;
            }

            // Save pointers for splicing.
            PB_Field* prev = current->prev;
            PB_Field* next = current->next;

            // Splice the unpacked fields into the message.
            prev->next = new_head;
            new_head->prev = prev;
            new_tail->next = next;
            next->prev = new_tail;

            // Free the original packed field's allocated buffer and the field itself.
            free(current->value.bytes.buf);
            PB_Field* to_free = current;
            // Move current to the next node (which is already linked).
            current = next;
            free(to_free);

            // Continue processing the message in case more packed fields exist.
            continue;
        }
        current = current->next;
    }
    return 0;
}

/**
 * @brief Output a human-readable representation of a protocol buffer field to a specified output stream.
 *
 * @details This function prints a description of the given PB_Field structure. The output includes the field number,
 * the wire type (presented as a human-readable string), and the field value. For fields of type VARINT_TYPE and I64_TYPE,
 * the value is printed as an unsigned 64-bit integer. For fields of type I32_TYPE, the value is printed as a 32-bit unsigned integer.
 * For fields of type LEN_TYPE, the function prints the length of the data and then the content enclosed in quotes.
 * Non-printable characters in a LEN_TYPE field are printed as hexadecimal escapes (e.g., "\x00").
 * For other types (e.g., SGROUP_TYPE, EGROUP_TYPE, SENTINEL_TYPE, or unknown types), a default message is printed.
 * If either the field pointer or the output stream is NULL, the function returns immediately.
 *
 * @param fp Pointer to the PB_Field structure to be displayed.
 * @param out Output stream (typically stdout) where the representation is printed.
 */

void PB_show_field(PB_Field* fp, FILE* out)
{
    if (!fp || !out) return;

    // Map the wire types to human-readable strings:
    const char* type_str;
    switch (fp->type)
    {
    case VARINT_TYPE:   type_str = "VARINT";   break;
    case I64_TYPE:      type_str = "I64";      break;
    case LEN_TYPE:      type_str = "LEN";      break;
    case SGROUP_TYPE:   type_str = "SGROUP";   break;
    case EGROUP_TYPE:   type_str = "EGROUP";   break;
    case I32_TYPE:      type_str = "I32";      break;
    case SENTINEL_TYPE: type_str = "SENTINEL"; break;
    default:            type_str = "UNKNOWN";  break;
    }

    // Print a header line for debugging
    fprintf(out,
        "DEBUG: PB_show_field -> Field Number: %d, Wire Type: %d (%s)\n",
        fp->number, fp->type, type_str);

    // Switch on wire type
    if (fp->type == VARINT_TYPE || fp->type == I64_TYPE)
    {
        // 64-bit integer (varint or i64)
        fprintf(out, "  Value (int64/varint): %llu\n",
            (unsigned long long)fp->value.i64);
    }
    else if (fp->type == I32_TYPE)
    {
        // 32-bit integer
        fprintf(out, "  Value (int32): %u\n", fp->value.i32);
    }
    else if (fp->type == LEN_TYPE)
    {
        size_t size = fp->value.bytes.size;
        char* buf   = fp->value.bytes.buf;

        fprintf(out, "  Value (LEN), size: %zu\n", size);

        // Check if it is EXACTLY the 9-byte "OSMHeader" string
        if (size == 9 && memcmp(buf, "OSMHeader", 9) == 0)
        {
            // It's the blob-header-type string "OSMHeader", not a sub-message
            fprintf(out,
                "  -- Detected \"OSMHeader\" string. Skipping sub-message parse.\n");
            fprintf(out, "  -- Content: \"OSMHeader\"\n");
        }
        else
        {
            // This LEN field might be a sub-message or raw data
            PB_Message embedded_msg = NULL;
            int ret = PB_read_embedded_message(buf, size, &embedded_msg);

            if (ret == 0 && embedded_msg != NULL)
            {
                fprintf(out, "  -- Parsing as embedded sub-message...\n");
                PB_show_message(embedded_msg, out);
                PB_delete_message(embedded_msg);
            }
            else
            {
                // If parse fails, just show raw data
                fprintf(out,
                    "  -- Could NOT parse as sub-message. Showing raw bytes in hex:\n");
                fprintf(out, "     ");
                for (size_t i = 0; i < size; i++)
                {
                    fprintf(out, "%02X ", (unsigned char)buf[i]);
                }
                fprintf(out, "\n");
            }
        }
    }
    else
    {
        // For SGROUP_TYPE, EGROUP_TYPE, SENTINEL_TYPE, or unknown
        fprintf(out,
            "  Value: [unsupported or sentinel wire type]\n");
    }

    // A small separator line for clarity
    fprintf(out, "----------------------\n");
    fflush(out);
}

/**
 * @brief  Output a human-readable representation of a PB_Message object.
 *
 * This function prints out each field in the message, including its number,
 * type, and value. It is useful for debugging protobuf parsing issues.
 *
 * @param msg The PB_Message object to be displayed.
 * @param out The output stream to print the message (e.g., stderr).
 */
/**
 * @brief  Output a human-readable representation of a PB_Message object.
 *
 * This function prints out each field in the message, including its number,
 * type, and value. If a field contains bytes, it prints them as a readable
 * hex dump instead of raw binary.
 *
 * @param msg The PB_Message object to be displayed.
 * @param out The output stream to print the message (e.g., stderr).
 */

void PB_show_message(PB_Message msg, FILE *out)
{
    if (!msg || !out)
    {
        fprintf(stderr, "ERROR: PB_show_message received NULL pointer.\n");
        return;
    }

    fprintf(out, "===== PB_Message Dump =====\n");

    PB_Field *field = msg->next; // Start after sentinel

    while (field)
    {
        // Ensure field is valid before accessing its properties
        if (field->type == SENTINEL_TYPE)
        {
            break;
        }

        fprintf(out, "Field Number: %d | Type: %d | ", field->number, field->type);

        switch (field->type)
        {
        case VARINT_TYPE:
            fprintf(out, "Value (int64): %lld\n", (long long)field->value.i64);
            break;

        case I64_TYPE:
            fprintf(out, "Value (int64): %lld\n", (long long)field->value.i64);
            break;

        case I32_TYPE:
            fprintf(out, "Value (int32): %d\n", field->value.i32);
            break;

        case LEN_TYPE:
            if (field->value.bytes.buf && field->value.bytes.size > 0)
            {
                fprintf(out, "Value (bytes, size: %zu)\n", field->value.bytes.size);

                // Attempt to parse the embedded message
                PB_Message embedded_msg = NULL;
                if (PB_read_embedded_message(field->value.bytes.buf, field->value.bytes.size, &embedded_msg) == 0 &&
                    embedded_msg)
                {
                    fprintf(out, "  -- Embedded Message Start --\n");
                    PB_show_message(embedded_msg, out); // Recursively print the embedded message
                    fprintf(out, "  -- Embedded Message End --\n");
                    PB_delete_message(embedded_msg); // Free embedded message after printing
                }
                else
                {
                    fprintf(out, "  -- Raw Data (Not Decoded) --\n");
                }
            }
            else
            {
                fprintf(out, "  -- Invalid LEN_TYPE field (empty or NULL buffer) --\n");
            }
            break;

        default:
            fprintf(out, "Unknown Type\n");
            break;
        }

        field = field->next; // Move to the next field
    }

    fprintf(out, "===== End of Message =====\n");
}

/**
 * @brief Free all memory associated with a PB_Message.
 *
 * A PB_Message is implemented as a circular, doubly linked list of PB_Field
 * structures. The list is headed by a special "sentinel" node whose type is
 * SENTINEL_TYPE. The sentinel does not contain meaningful data but is used to
 * mark the beginning and end of the list.
 *
 * This function iterates over the list (starting after the sentinel node),
 * freeing any dynamically allocated memory within each field. In particular:
 *   - For fields of type LEN_TYPE, it frees the memory allocated for the value
 *     (i.e., the buffer pointed to by value.bytes.buf).
 *   - Each PB_Field structure (except the sentinel) is then freed.
 * Finally, the sentinel node itself is freed.
 *
 * @param msg Pointer to the PB_Message (i.e., the sentinel node) to free.
 *            If msg is NULL, no action is taken.
 */
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Frees a PB_Message and its associated fields safely.
 *
 * This function ensures safe deallocation of dynamically allocated fields,
 * including preventing infinite loops due to list corruption.
 *
 * @param msg The PB_Message to be deleted.
 */
void PB_delete_message(PB_Message msg) {
    if (!msg) return;

    PB_Field *current = msg->next;
    while (current != msg) {
        if (!current) {
            fprintf(stderr, "ERROR: Null pointer encountered in PB_delete_message.\n");
            return;
        }
        if (current->next && (current->next < (PB_Field*)0x1000 || current->next > (PB_Field*)0xFFFFFFFFFFFF)) {
            fprintf(stderr, "ERROR: Invalid pointer detected in PB_delete_message: %p\n", current->next);
            return;
        }

        PB_Field *next_field = current->next;
        if (current->type == LEN_TYPE && current->value.bytes.buf != NULL) {
            free(current->value.bytes.buf);
        }
        free(current);
        current = next_field;
    }
    free(msg);
}