#include <stdio.h>
#include <stdlib.h>
#include "global.h"      // For USAGE(...) macro, help_requested, osm_input_file, process_args
#include "osm.h"         // For OSM_Map, OSM_read_Map
#include "debug.h"       // If you use debug(...) macros

int main(int argc, char **argv)
{
    // --------------------------
    // First pass: Validate args
    // --------------------------
    int rc = process_args(argc, argv, NULL);
    if (rc < 0) {
        USAGE(argv[0], EXIT_FAILURE);
    }

    // If '-h' was requested, print usage and exit success
    if (help_requested) {
        USAGE(argv[0], EXIT_SUCCESS);
    }

    // --------------------------
    // Open file if specified
    // --------------------------
    FILE *in = stdin;
    if (osm_input_file) {
        in = fopen(osm_input_file, "rb");
        if (!in) {
            debug("DEBUG: Could not open file '%s'\n", osm_input_file);
            return EXIT_FAILURE;
        }
    }

    // --------------------------
    // Read the map (OSM PBF data) **ONLY IF IT'S NOT ALREADY LOADED**
    // --------------------------
    static OSM_Map *map = NULL;
    if (!map) {
        debug("DEBUG: Reading OSM Map...\n");
        map = OSM_read_Map(in);
        if (!map) {
            debug("DEBUG: Failed to read map data.\n");
            if (in != stdin) fclose(in);
            return EXIT_FAILURE;
        }
    }

    // Close file if it isn't stdin
    if (in != stdin) {
        fclose(in);
    }

    // --------------------------
    // Second pass: Perform queries **USING LOADED MAP**
    // --------------------------
    rc = process_args(argc, argv, map);
    if (rc < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */

