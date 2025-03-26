# OSM PBF Parser

A lightweight, efficient parser for OpenStreetMap's Protocol Buffer Binary Format (PBF), implemented in C with no
external protobuf dependencies. This tool reads raw `.pbf` map data, decodes compressed protocol buffer messages using
zlib, and extracts structured information such as nodes, ways, and bounding box coordinates into custom C data
structures for command-line querying and analysis.

---

## Table of Contents

- [Features](#features)
- [Technologies Used](#technologies-used)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Example Output](#example-output)
- [Testing](#testing)
- [References](#references)
- [License](#license)

---

## Features

- **Protocol Buffer Decoding**  
  Custom implementation for reading protobuf wire formats (varint, fixed, length-delimited).

- **Zlib Decompression**  
  Handles zlib-compressed OSM blobs and decompresses them on the fly.

- **ZigZag and Delta Decoding**  
  Fully supports zigzag-encoded integers and delta-coded sequences used in DenseNodes and Ways.

- **Map Entity Extraction**  
  Parses and stores:
    - Bounding box (min/max lat/lon)
    - DenseNodes (with lat/lon in nanodegrees)
    - Ways (with node references and key/value tag mappings)

- **Command-Line Query Interface**  
  Supports structured queries such as:
    - Summary view of nodes and ways
    - Bounding box output
    - Node lookup by ID
    - Way lookup with optional key filters

- **Memory-Efficient Data Structures**  
  Custom linked structures ensure low-overhead parsing and fast access to parsed data.

---

## Technologies Used

- **C**
- **zlib**
- **Custom Protocol Buffer decoding (no libprotobuf)**
- **GNU Make**
- **Criterion (for unit testing)**

---

## Usage

### Build

```bash
make
```

### Run Help Menu

```bash
bin/pbf -h
```

### Example Queries

```bash
bin/pbf -f rsrc/sbu.pbf -s
bin/pbf -f rsrc/sbu.pbf -b
bin/pbf -f rsrc/sbu.pbf -n 213352011
bin/pbf -f rsrc/sbu.pbf -w 20175414 highway surface
```

---

## Project Structure

```
.
├── bin/                     # Compiled binaries (pbf, pbf_tests)
├── include/                # Public headers (DO NOT MODIFY tags respected)
├── src/                    # Core source files
├── tests/                  # Criterion-based test suite
├── rsrc/                   # Sample input PBF files
├── Makefile                # Build configuration
└── README.md               # This file
```

---

## Example Output

```
$ bin/pbf -f rsrc/sbu.pbf -s
nodes: 46415, ways: 5812

$ bin/pbf -f rsrc/sbu.pbf -b
min_lon: -73.138730, max_lon: -73.107490, max_lat: 40.928950, min_lat: 40.904040

$ bin/pbf -f rsrc/sbu.pbf -n 213352011
213352011	40.925193 -73.133857

$ bin/pbf -f rsrc/sbu.pbf -w 20175414 highway surface
20175414	service asphalt
```

---

## Testing

```bash
make clean debug
bin/pbf_tests --verbose=0
```

Includes unit tests for protocol buffer decoding, node/way accessors, and argument validation.

---

## References

- [OpenStreetMap PBF Format](https://wiki.openstreetmap.org/wiki/PBF_Format)
- [Google Protocol Buffers: Encoding Guide](https://protobuf.dev/programming-guides/encoding/)
- [zlib Manual](https://www.zlib.net/manual.html)
- [Beej's Guide to C](https://beej.us/guide/bgc/)

---

## License

This project is for educational and diagnostic use only. Attribution is appreciated if reused.

---
