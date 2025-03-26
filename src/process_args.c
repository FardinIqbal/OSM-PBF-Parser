#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "osm.h"
#include "debug.h"

/* Variable to be set by process_args if the '-h' flag is seen. */
int help_requested = 0;

/* Variable to be set by process_args to any filename specified with '-f'. */
char* osm_input_file = NULL;

int process_args(int argc, char** argv, OSM_Map* mp)
{
    int i = 1;
    int f_specified = 0;
    int summary_requested = 0;
    int bounding_box_requested = 0;
    OSM_Id node_id = 0;
    OSM_Id way_id = 0;
    int num_way_keys = 0;
    char* way_keys[10]; // Store keys for -w queries

    /* --- PHASE 1: Argument Validation --- */
    if (argc < 2)
    {
        USAGE(argv[0], EXIT_FAILURE);
        return -1;
    }

    if (strcmp(argv[1], "-h") == 0)
    {
        help_requested = 1;
        USAGE(argv[0], EXIT_SUCCESS);
        return 0;
    }

    while (i < argc)
    {
        if (strcmp(argv[i], "-f") == 0)
        {
            if ((i + 1) >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "ERROR: -f requires a filename.\n");
                return -1;
            }
            if (f_specified)
            {
                fprintf(stderr, "ERROR: Multiple -f options specified.\n");
                return -1;
            }
            osm_input_file = argv[i + 1];
            f_specified = 1;
            i += 2;
        }
        else if (strcmp(argv[i], "-s") == 0)
        {
            summary_requested = 1;
            i++;
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            bounding_box_requested = 1;
            i++;
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            if ((i + 1) >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "ERROR: -n requires a node ID.\n");
                return -1;
            }
            node_id = atoll(argv[i + 1]);
            i += 2;
        }
        else if (strcmp(argv[i], "-w") == 0)
        {
            if ((i + 1) >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "ERROR: -w requires a way ID.\n");
                return -1;
            }
            way_id = atoll(argv[i + 1]);
            i += 2;
            num_way_keys = 0;

            while (i < argc && argv[i][0] != '-')
            {
                if (num_way_keys < 10)
                {
                    way_keys[num_way_keys++] = argv[i];
                }
                else
                {
                    fprintf(stderr, "ERROR: Too many keys for -w (max 10 allowed).\n");
                    return -1;
                }
                i++;
            }
        }
        else
        {
            fprintf(stderr, "ERROR: Unknown argument: %s\n", argv[i]);
            return -1;
        }
    }

    /* --- PHASE 2: Query Execution (Only if mp is provided) --- */
    if (!mp) return 0;

    if (summary_requested)
    {
        printf("nodes: %d, ways: %d\n", OSM_Map_get_num_nodes(mp), OSM_Map_get_num_ways(mp));
    }

    if (bounding_box_requested)
    {
        OSM_BBox* bbox = OSM_Map_get_BBox(mp);
        if (bbox)
        {
            printf("min_lon: %.9f, max_lon: %.9f, max_lat: %.9f, min_lat: %.9f\n",
                   OSM_BBox_get_min_lon(bbox) / 1e9,
                   OSM_BBox_get_max_lon(bbox) / 1e9,
                   OSM_BBox_get_max_lat(bbox) / 1e9,
                   OSM_BBox_get_min_lat(bbox) / 1e9);
        }
    }

    if (node_id != 0)
    {
        OSM_Node* node = NULL;
        int num_nodes = OSM_Map_get_num_nodes(mp);
        for (int j = 0; j < num_nodes; j++)
        {
            OSM_Node* current_node = OSM_Map_get_Node(mp, j);
            if (OSM_Node_get_id(current_node) == node_id)
            {
                node = current_node;
                break;
            }
        }

        if (node)
        {
            // Get raw nanodegree values
            long lat_raw = OSM_Node_get_lat(node);
            long lon_raw = OSM_Node_get_lon(node);

            // Convert nanodegrees to degrees (divide by 1e7)
            double lat_in_degrees = lat_raw / 1e7;
            double lon_in_degrees = lon_raw / 1e7;

            // Print the converted values
            printf("%ld\t%.7f %.7f\n", OSM_Node_get_id(node), lat_in_degrees, lon_in_degrees);
        }
        else
        {
            printf("Node %ld not found.\n", node_id);
        }
    }


    if (way_id != 0)
    {
        OSM_Way* way = NULL;
        int num_ways = OSM_Map_get_num_ways(mp);
        for (int j = 0; j < num_ways; j++)
        {
            OSM_Way* current_way = OSM_Map_get_Way(mp, j);
            if (OSM_Way_get_id(current_way) == way_id)
            {
                way = current_way;
                break;
            }
        }

        if (way)
        {
            // If keys are specified, process key-value query first
            if (num_way_keys > 0)
            {
                // Print the way ID for the key/value output
                printf("%ld\t", OSM_Way_get_id(way));

                int found_key = 0;

                // Loop through all requested keys
                for (int k = 0; k < num_way_keys; k++)
                {
                    char* requested_key = way_keys[k];

                    // Check if any of the keys in the way match the requested keys
                    for (int j = 0; j < OSM_Way_get_num_keys(way); j++)
                    {
                        if (strcmp(OSM_Way_get_key(way, j), requested_key) == 0)
                        {
                            // If found, print the corresponding value
                            if (found_key)
                            {
                                printf(" "); // Separate values with a space if there were previous values
                            }
                            printf("%s", OSM_Way_get_value(way, j));
                            found_key = 1;
                        }
                    }
                }

                // If no keys were found, print a tab as per specification
                if (!found_key)
                {
                    printf("\t");
                }

                // Print a newline after the keys/values are printed
                printf("\n");
            }
            else
            {
                // If no keys are specified, print the way ID and node references
                printf("%ld\t", OSM_Way_get_id(way));
                for (int j = 0; j < OSM_Way_get_num_refs(way); j++)
                {
                    printf("%ld ", OSM_Way_get_ref(way, j));
                }
                printf("\n");
            }
        }
    }

    return 0;
}
