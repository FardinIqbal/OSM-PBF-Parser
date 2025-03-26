#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "global.h"
#include "protobuf.h"
#include "osm.h"
#include "debug.h"
#include <arpa/inet.h>  // for ntohl()

/* =======================
 * Internal OSM Data Structures
 * =======================*/

// Structure representing a bounding box (in nanodegrees).
struct OSM_BBox {
    int64_t min_lon;
    int64_t max_lon;
    int64_t min_lat;
    int64_t max_lat;
};

// Structure representing a node in the map.
struct OSM_Node {
    int64_t id;         // Unique identifier for the node.
    int64_t lat;        // Latitude (in nanodegrees).
    int64_t lon;        // Longitude (in nanodegrees).
    int num_keys;       // Number of key/value pairs.
    char **keys;        // Array of keys (null-terminated strings).
    char **vals;        // Array of values (null-terminated strings).
};

// Structure representing a way in the map.
struct OSM_Way {
    int64_t id;         // Unique identifier for the way.
    int num_refs;       // Number of node references in the way.
    int64_t *refs;      // Array of node IDs that form the way.
    int num_keys;       // Number of key/value pairs.
    char **keys;        // Array of keys (null-terminated strings).
    char **vals;        // Array of values (null-terminated strings).
};

// Structure representing the entire map.
struct OSM_Map {
    OSM_BBox *bbox;     // Pointer to the bounding box (if any).
    int num_nodes;      // Total number of nodes.
    OSM_Node **nodes;   // Array of pointers to nodes (can be NULL if not populated).
    int num_ways;       // Total number of ways.
    OSM_Way **ways;     // Array of pointers to ways (can be NULL if not populated).
};


/*************************************
 * Forward Declarations
 *************************************/
static uint32_t read_uint32_be(FILE *in);
static int parse_HeaderBlock(PB_Message pb_msg, OSM_Map *map);
static int parse_PrimitiveBlock(PB_Message pb_msg, OSM_Map *map);

// Helpers for parse_PrimitiveBlock:
static int expand_string_table(PB_Message block_msg, char ***out_strings, int *out_count);
static int parse_ways_in_group(PB_Message group_msg, OSM_Map *map,
                               char **stringtable, int string_count);
static int64_t zigzag_decode(int64_t val);



/* ===========================
 * Function Implementations
 * ===========================*/


// /**
//  * @brief Finds a way by its ID.
//  *
//  * This function searches for a way in the map by its unique ID.
//  *
//  * @param mp The map to search in.
//  * @param way_id The way ID to search for.
//  * @return A pointer to the OSM_Way if found, otherwise NULL.
//  */
// OSM_Way *find_way_by_id(OSM_Map *mp, OSM_Id way_id) {
//     if (!mp || !mp->ways) {
//         return NULL;
//     }
//
//     for (int i = 0; i < mp->num_ways; i++) {
//         if (mp->ways[i] && mp->ways[i]->id == way_id) {
//             return mp->ways[i];
//         }
//     }
//
//     return NULL;  // Not found
// }



/**
 * @brief Decode a zigzag-encoded 64-bit integer.
 *
 * Zigzag encoding maps signed integers to unsigned integers so that small absolute values (both positive and negative)
 * have small varint encodings. This function reverses that process.
 *
 * @param val The zigzag-encoded 64-bit integer.
 * @return The decoded signed integer.
 */
static int64_t zigzag_decode(int64_t val) {
    return (val >> 1) ^ -(val & 1);
}

/**
 * @brief Read a 4-byte big-endian unsigned integer from the stream.
 *
 * This function reads 4 bytes from the input stream, interprets them as a big-endian
 * unsigned integer, converts it to host byte order, and returns the result.
 *
 * @param in The input stream.
 * @return The 32-bit unsigned integer in host byte order, or 0 on error.
 */

/**
 * @brief Read a 4-byte big-endian unsigned integer from the stream.
 *
 * @param in The input stream.
 * @return The 32-bit unsigned integer in host byte order, or UINT32_MAX on error.
 */
/**
 * @brief Read a 4-byte big-endian unsigned integer from the stream.
 *
 * This function reads 4 bytes from the input stream, interprets them as a big-endian
 * unsigned integer, converts it to host byte order, and returns the result.
 *
 * @param in The input stream.
 * @return The 32-bit unsigned integer in host byte order, or UINT32_MAX on error.
 */
uint32_t read_uint32_be(FILE *in) {
    unsigned char buf[4];
    if (fread(buf, 1, 4, in) != 4) {
        if (feof(in)) {
            debug("DEBUG: EOF reached. No more data to read.\n");
            return UINT32_MAX;  // Indicate EOF
        }
        fprintf(stderr, "ERROR: Failed to read 4-byte length (possibly corrupt data)\n");
        return UINT32_MAX;  // Indicate error
    }
    uint32_t tmp;
    memcpy(&tmp, buf, 4);
    return ntohl(tmp);  // Convert from big-endian
}


/**
 * @brief Parse a HeaderBlock from a blob message and extract the bounding box.
 *
 * The HeaderBlock (from an OSMHeader blob) contains a sub-message with four fields:
 *   - Field #1: min_lon (zigzag-encoded)
 *   - Field #2: max_lon (zigzag-encoded)
 *   - Field #3: max_lat (zigzag-encoded)
 *   - Field #4: min_lat (zigzag-encoded)
 *
 * This function decodes the above values using zigzag decoding and stores them
 * in the map's bounding box structure (`map->bbox`).
 *
 * @param pb_msg  The parsed Blob message for the header, which contains the bounding box.
 * @param map     Pointer to the OSM_Map structure that will be updated with the bounding box.
 * @return 0 on success, -1 if any error occurs during parsing or memory allocation.
 */
static int parse_HeaderBlock(PB_Message pb_msg, OSM_Map *map)
{
    if (!pb_msg || !map) {
        return -1;
    }
    debug("DEBUG: parse_HeaderBlock invoked.\n");

    // #1 => HeaderBBox
    PB_Field *bbox_field = PB_get_field(pb_msg, 1, LEN_TYPE);
    if (!bbox_field) {
        debug("DEBUG: No bounding box field in HeaderBlock.\n");
        return 0;
    }

    PB_Message bbox_msg = NULL;
    if (PB_read_embedded_message(bbox_field->value.bytes.buf,
                                 bbox_field->value.bytes.size,
                                 &bbox_msg) < 0 || !bbox_msg)
    {
        debug("ERROR: Could not parse bbox sub-message.\n");
        return -1;
    }

    PB_Field *min_lon_f = PB_get_field(bbox_msg, 1, VARINT_TYPE);
    PB_Field *max_lon_f = PB_get_field(bbox_msg, 2, VARINT_TYPE);
    PB_Field *max_lat_f = PB_get_field(bbox_msg, 3, VARINT_TYPE);
    PB_Field *min_lat_f = PB_get_field(bbox_msg, 4, VARINT_TYPE);

    if (!min_lon_f || !max_lon_f || !max_lat_f || !min_lat_f) {
        debug("DEBUG: Bounding box fields missing. Possibly partial.\n");
        PB_delete_message(bbox_msg);
        return 0;
    }

    map->bbox = calloc(1, sizeof(OSM_BBox));
    if (!map->bbox) {
        debug("ERROR: Out of memory for map->bbox.\n");
        PB_delete_message(bbox_msg);
        return -1;
    }

    map->bbox->min_lon = zigzag_decode(min_lon_f->value.i64);
    map->bbox->max_lon = zigzag_decode(max_lon_f->value.i64);
    map->bbox->max_lat = zigzag_decode(max_lat_f->value.i64);
    map->bbox->min_lat = zigzag_decode(min_lat_f->value.i64);

    debug("DEBUG: HeaderBlock -> min_lon=%lld, max_lon=%lld, "
          "max_lat=%lld, min_lat=%lld\n",
          (long long)map->bbox->min_lon, (long long)map->bbox->max_lon,
          (long long)map->bbox->max_lat, (long long)map->bbox->min_lat);

    PB_delete_message(bbox_msg);
    return 0;
}

/**
 * @brief Read map data in OSM PBF format from the specified input stream,
 * construct and return a corresponding OSM_Map object.  Storage required
 * for the map object and any related entities is allocated on the heap.
 * @param in  The input stream to read.
 * @return  If reading was successful, a pointer to the OSM_Map object constructed
 * from the input, otherwise NULL in case of any error.
 */

OSM_Map *OSM_read_Map(FILE *in)
{
    if (!in) return NULL;

    debug("DEBUG: Entering OSM_read_Map()\n");

    OSM_Map *map = calloc(1, sizeof(OSM_Map));
    if (!map) {
        fprintf(stderr, "ERROR: OSM_read_Map - out of memory.\n");
        return NULL;
    }

    while (1) {
        // 1) Read 4-byte BlobHeader length (big-endian)
        uint32_t blob_header_len = read_uint32_be(in);
        if (blob_header_len == 0 || feof(in)) {
            debug("DEBUG: No more data or EOF reached. Stopping.\n");
            break;
        }

        // 2) Read BlobHeader
        char *header_buf = malloc(blob_header_len);
        if (!header_buf) {
            fprintf(stderr, "ERROR: OSM_read_Map - out of memory reading header.\n");
            free(map);
            return NULL;
        }

        size_t read_size = fread(header_buf, 1, blob_header_len, in);
        if (read_size != blob_header_len || feof(in)) {
            fprintf(stderr, "ERROR: OSM_read_Map - failed to read blob header.\n");
            free(header_buf);
            free(map);
            return NULL;
        }

        // 3) Parse BlobHeader
        PB_Message header_msg = NULL;
        if (PB_read_embedded_message(header_buf, blob_header_len, &header_msg) < 0 || !header_msg) {
            fprintf(stderr, "ERROR: OSM_read_Map - could not parse BlobHeader.\n");
            free(header_buf);
            free(map);
            return NULL;
        }
        free(header_buf);

        // Extract fields from BlobHeader
        PB_Field *type_field = PB_get_field(header_msg, 1, LEN_TYPE);
        PB_Field *datasize_field = PB_get_field(header_msg, 3, VARINT_TYPE);

        if (!type_field || !datasize_field) {
            fprintf(stderr, "ERROR: BlobHeader missing type or datasize.\n");
            PB_delete_message(header_msg);
            free(map);
            return NULL;
        }

        size_t datasize = (size_t)datasize_field->value.i64;

        // Convert type field to string
        char *type_str = malloc(type_field->value.bytes.size + 1);
        if (!type_str) {
            fprintf(stderr, "ERROR: OSM_read_Map - out of memory for type_str.\n");
            PB_delete_message(header_msg);
            free(map);
            return NULL;
        }
        memcpy(type_str, type_field->value.bytes.buf, type_field->value.bytes.size);
        type_str[type_field->value.bytes.size] = '\0';

        PB_delete_message(header_msg);
        debug("DEBUG: Blob type: %s, DataSize: %zu\n", type_str, datasize);

        // 4) Read the blob data
        if (datasize == 0) {
            free(type_str);
            debug("DEBUG: Empty blob detected, skipping.\n");
            continue;
        }

        char *blob_buf = malloc(datasize);
        if (!blob_buf) {
            fprintf(stderr, "ERROR: OSM_read_Map - out of memory for blob.\n");
            free(type_str);
            free(map);
            return NULL;
        }

        read_size = fread(blob_buf, 1, datasize, in);
        if (read_size != datasize || feof(in)) {
            fprintf(stderr, "ERROR: OSM_read_Map - failed to read blob data.\n");
            free(type_str);
            free(blob_buf);
            free(map);
            return NULL;
        }

        // 5) Parse Blob
        PB_Message blob_msg = NULL;
        if (PB_read_embedded_message(blob_buf, datasize, &blob_msg) < 0 || !blob_msg) {
            fprintf(stderr, "ERROR: OSM_read_Map - failed to parse Blob.\n");
            free(type_str);
            free(blob_buf);
            free(map);
            return NULL;
        }
        free(blob_buf);

        // 6) Handle compressed or raw data
        PB_Field *zlib_data_field = PB_get_field(blob_msg, 3, LEN_TYPE);
        PB_Field *raw_field = PB_get_field(blob_msg, 1, LEN_TYPE);
        PB_Message uncompressed_msg = NULL;

        if (zlib_data_field) {
            debug("DEBUG: Decompressing zlib_data.\n");
            if (PB_inflate_embedded_message(zlib_data_field->value.bytes.buf,
                                            zlib_data_field->value.bytes.size,
                                            &uncompressed_msg) < 0 || !uncompressed_msg) {
                fprintf(stderr, "ERROR: OSM_read_Map - inflate zlib_data failed.\n");
                PB_delete_message(blob_msg);
                free(type_str);
                free(map);
                return NULL;
            }
        } else if (raw_field) {
            debug("DEBUG: Parsing raw blob data.\n");
            if (PB_read_embedded_message(raw_field->value.bytes.buf,
                                         raw_field->value.bytes.size,
                                         &uncompressed_msg) < 0 || !uncompressed_msg) {
                fprintf(stderr, "ERROR: OSM_read_Map - parse raw blob data failed.\n");
                PB_delete_message(blob_msg);
                free(type_str);
                free(map);
                return NULL;
            }
        } else {
            fprintf(stderr, "ERROR: OSM_read_Map - neither raw nor zlib_data found.\n");
            PB_delete_message(blob_msg);
            free(type_str);
            free(map);
            return NULL;
        }

        // Process OSMHeader or OSMData
        if (strcmp(type_str, "OSMHeader") == 0) {
            debug("DEBUG: Processing OSMHeader...\n");
            if (parse_HeaderBlock(uncompressed_msg, map) < 0) {
                debug("WARN: parse_HeaderBlock failed.\n");
            }
        } else if (strcmp(type_str, "OSMData") == 0) {
            debug("DEBUG: Processing OSMData...\n");
            if (parse_PrimitiveBlock(uncompressed_msg, map) < 0) {
                debug("WARN: parse_PrimitiveBlock failed.\n");
            }
        } else {
            debug("DEBUG: Unknown blob type \"%s\". Skipping.\n", type_str);
        }

        PB_delete_message(blob_msg);
        PB_delete_message(uncompressed_msg);
        free(type_str);

        if (feof(in)) {
            debug("DEBUG: Reached EOF after processing blobs. Exiting loop.\n");
            break;
        }
    }

    debug("DEBUG: Successfully exited OSM_read_Map(), returning map.\n");
    return map;
}

/**
 * @brief Parse repeated "Way" entries from a group message and store the
 *        parsed data into the given OSM_Map object.
 *
 * This function extracts information about "Way" elements from the group message.
 * For each "Way" object:
 *   - Parses the ID field.
 *   - Expands packed fields for keys, values, and references.
 *   - Decodes the reference IDs using delta coding and zigzag encoding.
 *   - Stores the parsed "Way" object into the OSM_Map's list of ways.
 *
 * @param group_msg  The group message containing the "Way" objects to be parsed.
 * @param map        The map structure where the parsed "Way" objects will be stored.
 * @param stringtable The string table containing the keys and values for the way.
 * @param string_count The number of strings in the string table.
 * @return 0 if parsing was successful, otherwise a non-zero error code.
 */
static int parse_ways_in_group(PB_Message group_msg, OSM_Map *map,
                               char **stringtable, int string_count)
{
    // repeated Way => field #3
    PB_Field *way_field = NULL;
    while ((way_field = PB_next_field(
                (way_field ? way_field : group_msg),
                 3, LEN_TYPE, FORWARD_DIR)))
    {
        PB_Message way_msg = NULL;
        if (PB_read_embedded_message(way_field->value.bytes.buf,
                                     way_field->value.bytes.size,
                                     &way_msg) < 0 || !way_msg)
        {
            debug("WARN: Can't parse Way sub-message.\n");
            continue;
        }

        // required int64 id = 1
        PB_Field *id_f = PB_get_field(way_msg, 1, VARINT_TYPE);
        if (!id_f) {
            debug("WARN: Way has no id (#1)?\n");
            PB_delete_message(way_msg);
            continue;
        }
        int64_t way_id = id_f->value.i64;

        // expand packed fields: keys (#2), vals (#3), refs (#8)
        PB_expand_packed_fields(way_msg, 2, VARINT_TYPE);
        PB_expand_packed_fields(way_msg, 3, VARINT_TYPE);
        PB_expand_packed_fields(way_msg, 8, VARINT_TYPE);

        // count how many key fields
        int num_keys = 0;
        PB_Field *kf = NULL;
        while ((kf = PB_next_field((kf ? kf : way_msg),
                                   2, VARINT_TYPE, FORWARD_DIR)))
        {
            num_keys++;
        }

        // allocate arrays for keys/vals
        char **way_keys = (num_keys > 0) ? calloc(num_keys, sizeof(char*)) : NULL;
        char **way_vals = (num_keys > 0) ? calloc(num_keys, sizeof(char*)) : NULL;

        // walk them in parallel
        kf = NULL;
        PB_Field *vf = NULL;
        int idx = 0;
        while ((kf = PB_next_field((kf ? kf : way_msg),
                                   2, VARINT_TYPE, FORWARD_DIR)) &&
               (vf = PB_next_field((vf ? vf : way_msg),
                                   3, VARINT_TYPE, FORWARD_DIR)))
        {
            uint32_t k_idx = (uint32_t)kf->value.i64;
            uint32_t v_idx = (uint32_t)vf->value.i64;
            // map them to stringtable
            if (k_idx < (uint32_t)string_count && way_keys) {
                way_keys[idx] = strdup(stringtable[k_idx]);
            }
            if (v_idx < (uint32_t)string_count && way_vals) {
                way_vals[idx] = strdup(stringtable[v_idx]);
            }
            idx++;
        }

        // parse refs (#8), repeated sint64 with delta
        int num_refs = 0;
        PB_Field *rf = NULL;
        while ((rf = PB_next_field((rf ? rf : way_msg),
                                   8, VARINT_TYPE, FORWARD_DIR)))
        {
            num_refs++;
        }

        int64_t *refs_array = (num_refs > 0) ? calloc(num_refs, sizeof(int64_t)) : NULL;

        // read them with delta
        int64_t running = 0;
        int r_idx = 0;
        rf = NULL;
        while ((rf = PB_next_field((rf ? rf : way_msg),
                                   8, VARINT_TYPE, FORWARD_DIR)))
        {
            // "refs" are stored as sint64 => zigzag
            // plus delta-coded
            int64_t encoded = rf->value.i64;
            // zigzag:
            int64_t delta = zigzag_decode(encoded);
            running += delta;
            if (refs_array) {
                refs_array[r_idx] = running;
            }
            r_idx++;
        }

        // build an OSM_Way
        OSM_Way *way = calloc(1, sizeof(OSM_Way));
        if (!way) {
            debug("ERROR: Out of memory for OSM_Way.\n");
            // clean up
            PB_delete_message(way_msg);
            if (way_keys) {
                for (int i=0; i<num_keys; i++) {
                    free(way_keys[i]);
                    free(way_vals[i]);
                }
                free(way_keys);
                free(way_vals);
            }
            free(refs_array);
            continue;
        }
        way->id        = way_id;
        way->num_keys  = num_keys;
        way->keys      = way_keys;
        way->vals      = way_vals;
        way->num_refs  = num_refs;
        way->refs      = refs_array;

        // insert it into map->ways
        map->num_ways++;
        map->ways = realloc(map->ways, map->num_ways * sizeof(OSM_Way*));
        map->ways[ map->num_ways - 1 ] = way;

        debug("DEBUG: Found Way id=%lld, keys=%d, refs=%d\n",
              (long long)way_id, num_keys, num_refs);

        PB_delete_message(way_msg);
    }

    return 0;
}

/**
 * @brief Parse nodes from a PrimitiveGroup and store them in the OSM_Map.
 *
 * - Nodes are found in field #1 (simple nodes) or field #2 (DenseNodes).
 * - DenseNodes encoding stores differences (deltas), which must be reconstructed.
 *
 * @param group_msg The PrimitiveGroup message.
 * @param map The OSM_Map structure to populate.
 */
static int parse_nodes_in_group(PB_Message group_msg, OSM_Map *map) {
    if (!group_msg || !map) {
        return -1;
    }

    PB_Field *dense_field = PB_get_field(group_msg, 2, LEN_TYPE); // DenseNodes
    if (!dense_field) {
        debug("DEBUG: No DenseNodes field found.\n");
        return 0;
    }

    PB_Message dense_msg = NULL;
    if (PB_read_embedded_message(dense_field->value.bytes.buf,
                                 dense_field->value.bytes.size,
                                 &dense_msg) < 0 || !dense_msg) {
        debug("ERROR: Could not parse DenseNodes sub-message.\n");
        return -1;
    }

    debug("DEBUG: Parsing DenseNodes...\n");

    // Expand packed fields (IDs, latitudes, longitudes)
    PB_expand_packed_fields(dense_msg, 1, VARINT_TYPE); // Node IDs
    PB_expand_packed_fields(dense_msg, 8, VARINT_TYPE); // Latitudes
    PB_expand_packed_fields(dense_msg, 9, VARINT_TYPE); // Longitudes

    // Extract and decode nodes
    int num_nodes = 0;
    PB_Field *id_field = NULL;
    PB_Field *lat_field = NULL;
    PB_Field *lon_field = NULL;

    int64_t last_id = 0;
    int64_t last_lat = 0;
    int64_t last_lon = 0;

    while ((id_field = PB_next_field((id_field ? id_field : dense_msg), 1, VARINT_TYPE, FORWARD_DIR)) &&
           (lat_field = PB_next_field((lat_field ? lat_field : dense_msg), 8, VARINT_TYPE, FORWARD_DIR)) &&
           (lon_field = PB_next_field((lon_field ? lon_field : dense_msg), 9, VARINT_TYPE, FORWARD_DIR))) {

        int64_t delta_id = id_field->value.i64;
        int64_t delta_lat = lat_field->value.i64;
        int64_t delta_lon = lon_field->value.i64;

        // Apply delta encoding and zigzag decoding
        last_id += zigzag_decode(delta_id);
        last_lat += zigzag_decode(delta_lat);
        last_lon += zigzag_decode(delta_lon);

        // Allocate and store the node
        OSM_Node *node = calloc(1, sizeof(OSM_Node));
        if (!node) {
            debug("ERROR: Out of memory for OSM_Node.\n");
            PB_delete_message(dense_msg);
            return -1;
        }

        node->id = last_id;
        node->lat = last_lat;
        node->lon = last_lon;

        // Store in map->nodes array
        map->num_nodes++;
        map->nodes = realloc(map->nodes, map->num_nodes * sizeof(OSM_Node *));
        map->nodes[map->num_nodes - 1] = node;

        debug("DEBUG: Read Node ID %lld at lat=%.7lf, lon=%.7lf\n",
              (long long)node->id, node->lat * 1e-7, node->lon * 1e-7);

        num_nodes++;
    }

    PB_delete_message(dense_msg);
    return num_nodes;
}

/* ===========================
 * Accessor Functions
 * ===========================*/

/**
 * @brief Get the number of nodes in an OSM_Map object.
 *
 * @param mp The map object to query.
 * @return The number of nodes, or 0 if mp is NULL.
 */
int OSM_Map_get_num_nodes(OSM_Map *mp) {
    return (mp) ? mp->num_nodes : 0;
}

/**
 * @brief Get the number of ways in an OSM_Map object.
 *
 * @param mp The map object to query.
 * @return The number of ways, or 0 if mp is NULL.
 */
int OSM_Map_get_num_ways(OSM_Map *mp) {
    return (mp) ? mp->num_ways : 0;
}

/**
 * @brief Get the node at the specified index from an OSM_Map object.
 *
 * @param mp The map to be queried.
 * @param index The index of the node to be retrieved.
 * @return The node at the specified index, or NULL if index is out of range or nodes not populated.
 */
OSM_Node *OSM_Map_get_Node(OSM_Map *mp, int index) {
    if (!mp || !mp->nodes || index < 0 || index >= mp->num_nodes) {
        return NULL;
    }
    return mp->nodes[index];
}

/**
 * @brief Get the way at the specified index from an OSM_Map object.
 *
 * @param mp The map to be queried.
 * @param index The index of the way to be retrieved.
 * @return The way at the specified index, or NULL if index is out of range or ways not populated.
 */
OSM_Way *OSM_Map_get_Way(OSM_Map *mp, int index) {
    if (!mp || !mp->ways || index < 0 || index >= mp->num_ways) {
        return NULL;
    }
    return mp->ways[index];
}

/**
 * @brief Get the bounding box of the specified OSM_Map object.
 *
 * @param mp The map object to be queried.
 * @return The pointer to the bounding box, or NULL if not set.
 */
OSM_BBox *OSM_Map_get_BBox(OSM_Map *mp) {
    return (mp) ? mp->bbox : NULL;
}

/**
 * @brief Get the id of an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @return The id of the node.
 */
int64_t OSM_Node_get_id(OSM_Node *np) {
    return (np) ? np->id : 0;
}

/**
 * @brief Get the latitude of an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @return The latitude (in nanodegrees) of the node.
 */
int64_t OSM_Node_get_lat(OSM_Node *np) {
    return (np) ? np->lat : 0;
}

/**
 * @brief Get the longitude of an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @return The longitude (in nanodegrees) of the node.
 */
int64_t OSM_Node_get_lon(OSM_Node *np) {
    return (np) ? np->lon : 0;
}

/**
 * @brief Get the number of keys (key/value pairs) in an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @return The number of keys, or 0 if np is NULL.
 */
int OSM_Node_get_num_keys(OSM_Node *np) {
    return (np) ? np->num_keys : 0;
}

/**
 * @brief Get the key at a specified index in an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @param index The index of the key.
 * @return The key as a null-terminated string, or NULL if index is out of range.
 */
char *OSM_Node_get_key(OSM_Node *np, int index) {
    if (!np || !np->keys || index < 0 || index >= np->num_keys) {
        return NULL;
    }
    return np->keys[index];
}

/**
 * @brief Get the value at a specified index in an OSM_Node object.
 *
 * @param np The node object to be queried.
 * @param index The index of the value.
 * @return The value as a null-terminated string, or NULL if index is out of range.
 */
char *OSM_Node_get_value(OSM_Node *np, int index) {
    if (!np || !np->vals || index < 0 || index >= np->num_keys) {
        return NULL;
    }
    return np->vals[index];
}



/**
 * @brief Get the id of an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @return The id of the way.
 */
int64_t OSM_Way_get_id(OSM_Way *wp) {
    return (wp) ? wp->id : 0;
}

/**
 * @brief Get the number of node references in an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @return The number of node references, or 0 if wp is NULL.
 */
int OSM_Way_get_num_refs(OSM_Way *wp) {
    return (wp) ? wp->num_refs : 0;
}

/**
 * @brief Get the node reference at a specified index in an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @param index The index of the node reference.
 * @return The node id at the specified index, or 0 if index is out of range.
 */
OSM_Id OSM_Way_get_ref(OSM_Way *wp, int index) {
    if (!wp || !wp->refs || index < 0 || index >= wp->num_refs) {
        return 0;
    }
    return wp->refs[index];
}

/**
 * @brief Get the number of keys (key/value pairs) in an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @return The number of keys, or 0 if wp is NULL.
 */
int OSM_Way_get_num_keys(OSM_Way *wp) {
    return (wp) ? wp->num_keys : 0;
}

/**
 * @brief Get the key at a specified index in an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @param index The index of the key.
 * @return The key as a null-terminated string, or NULL if index is out of range.
 */
char *OSM_Way_get_key(OSM_Way *wp, int index) {
    if (!wp || !wp->keys || index < 0 || index >= wp->num_keys) {
        return NULL;
    }
    return wp->keys[index];
}

/**
 * @brief Get the value at a specified index in an OSM_Way object.
 *
 * @param wp The way object to be queried.
 * @param index The index of the value.
 * @return The value as a null-terminated string, or NULL if index is out of range.
 */
char *OSM_Way_get_value(OSM_Way *wp, int index) {
    if (!wp || !wp->vals || index < 0 || index >= wp->num_keys) {
        return NULL;
    }
    return wp->vals[index];
}

/**
 * @brief Get the minimum longitude coordinate of an OSM_BBox object.
 *
 * @param bbp The bounding box to be queried.
 * @return The minimum longitude (in nanodegrees), or 0 if bbp is NULL.
 */
int64_t OSM_BBox_get_min_lon(OSM_BBox *bbp) {
    return (bbp) ? bbp->min_lon : 0;
}

/**
 * @brief Get the maximum longitude coordinate of an OSM_BBox object.
 *
 * @param bbp The bounding box to be queried.
 * @return The maximum longitude (in nanodegrees), or 0 if bbp is NULL.
 */
int64_t OSM_BBox_get_max_lon(OSM_BBox *bbp) {
    return (bbp) ? bbp->max_lon : 0;
}

/**
 * @brief Get the maximum latitude coordinate of an OSM_BBox object.
 *
 * @param bbp The bounding box to be queried.
 * @return The maximum latitude (in nanodegrees), or 0 if bbp is NULL.
 */
int64_t OSM_BBox_get_max_lat(OSM_BBox *bbp) {
    return (bbp) ? bbp->max_lat : 0;
}

/**
 * @brief Get the minimum latitude coordinate of an OSM_BBox object.
 *
 * @param bbp The bounding box to be queried.
 * @return The minimum latitude (in nanodegree), or 0 if bbp is NULL.
 */
int64_t OSM_BBox_get_min_lat(OSM_BBox *bbp) {
    return (bbp) ? bbp->min_lat : 0;
}



/**
 * @brief Parse an OSM "PrimitiveBlock" to extract ways and nodes.
 *
 * PrimitiveBlock {
 *   required StringTable stringtable = 1;
 *   repeated PrimitiveGroup primitivegroup = 2;
 *   optional int32 granularity = 17 [default=100];
 *   ...
 * }
 */
/*************************************
 * parse_PrimitiveBlock
 *
 * Minimal logic:
 *  - reads the StringTable (#1)
 *  - iterates repeated PrimitiveGroup (#2)
 *    => parse repeated Way (#3)
 *    => parse repeated DenseNodes (#2)
 *************************************/
static int parse_PrimitiveBlock(PB_Message pb_msg, OSM_Map *map)
{
    if (!pb_msg || !map) {
        return -1;
    }
    debug("DEBUG: parse_PrimitiveBlock invoked.\n");

    // 1) Expand string table
    char **stringtable = NULL;
    int string_count = 0;
    if (expand_string_table(pb_msg, &stringtable, &string_count) < 0) {
        debug("WARN: expand_string_table failed.\n");
        // not fatal
    }

    // 2) repeated PrimitiveGroup => field #2
    PB_Field *group_field = NULL;
    while ((group_field = PB_next_field(
                (group_field ? group_field : pb_msg),
                 2, LEN_TYPE, FORWARD_DIR)))
    {
        // parse each group sub-message
        PB_Message group_msg = NULL;
        if (PB_read_embedded_message(group_field->value.bytes.buf,
                                     group_field->value.bytes.size,
                                     &group_msg) < 0 || !group_msg)
        {
            debug("WARN: Could not parse group sub-message.\n");
            continue;
        }

        // parse nodes (#2)
        debug("DEBUG: Parsing nodes in group...\n");
        parse_nodes_in_group(group_msg, map);

        // parse ways (#3)
        parse_ways_in_group(group_msg, map, stringtable, string_count);

        PB_delete_message(group_msg);
    }

    // Free string table
    if (stringtable) {
        for (int i = 0; i < string_count; i++) {
            free(stringtable[i]);
        }
        free(stringtable);
    }

    return 0;
}


/**
 * @brief Expand the string table in a PrimitiveBlock into a `char**` array.
 *
 * The string table is in field #1 as a sub-message:
 *   StringTable {
 *     repeated bytes s = 1;
 *   }
 */
/*************************************
 * expand_string_table
 *
 * The string table is a sub-message
 *  field #1 => stringtable
 * Inside stringtable:
 *  repeated bytes s = 1;
 *************************************/
static int expand_string_table(PB_Message block_msg, char ***out_strings, int *out_count)
{
    if (!block_msg || !out_strings || !out_count) {
        return -1;
    }
    *out_strings = NULL;
    *out_count = 0;

    // sub-message is field #1
    PB_Field *st_field = PB_get_field(block_msg, 1, LEN_TYPE);
    if (!st_field) {
        debug("DEBUG: No stringtable field (#1) in PrimitiveBlock.\n");
        return 0; // not an error
    }

    PB_Message st_msg = NULL;
    if (PB_read_embedded_message(st_field->value.bytes.buf,
                                 st_field->value.bytes.size,
                                 &st_msg) < 0 || !st_msg)
    {
        debug("ERROR: Could not parse stringtable sub-msg.\n");
        return -1;
    }

    // repeated bytes s => #1
    // count how many
    int cnt = 0;
    PB_Field *f = NULL;
    while ((f = PB_next_field((f ? f : st_msg), 1, LEN_TYPE, FORWARD_DIR)))
    {
        cnt++;
    }
    if (cnt == 0) {
        PB_delete_message(st_msg);
        return 0;
    }

    // allocate array of char*
    char **strings = calloc(cnt, sizeof(char*));
    if (!strings) {
        PB_delete_message(st_msg);
        return -1;
    }

    // read them again
    int idx = 0;
    f = NULL;
    while ((f = PB_next_field((f ? f : st_msg), 1, LEN_TYPE, FORWARD_DIR)))
    {
        size_t sz = f->value.bytes.size;
        char *temp = calloc(sz+1, 1);
        if (!temp) {
            // keep going? or break
            continue;
        }
        memcpy(temp, f->value.bytes.buf, sz);
        strings[idx++] = temp;
    }

    PB_delete_message(st_msg);

    *out_strings = strings;
    *out_count   = cnt;
    debug("DEBUG: expand_string_table => %d strings.\n", cnt);
    return 0;
}