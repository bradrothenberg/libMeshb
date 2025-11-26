/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                     NASTRAN BDF TO MESHB CONVERTER                         */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Description:         Convert Nastran BDF/NAS files to meshb format         */
/* Author:              Claude / Anthropic                                    */
/* Creation date:       nov 25 2025                                           */
/*                                                                            */
/* Supported elements:                                                        */
/*   - GRID / GRID* (nodes)                                                   */
/*   - CTRIA3 (triangles)                                                     */
/*   - CQUAD4 (quadrilaterals)                                                */
/*   - CTETRA (tetrahedra)                                                    */
/*   - CPENTA (prisms/wedges)                                                 */
/*   - CHEXA (hexahedra)                                                      */
/*   - CPYRAM (pyramids)                                                      */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <libmeshb7.h>

/*----------------------------------------------------------------------------*/
/* Windows compatibility                                                      */
/*----------------------------------------------------------------------------*/

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

/*----------------------------------------------------------------------------*/
/* Data structures                                                            */
/*----------------------------------------------------------------------------*/

typedef struct {
    int id;
    double x, y, z;
} GridNode;

typedef struct {
    int id;
    int pid;
    int nodes[8];
} Element;

typedef struct {
    GridNode *nodes;
    int numNodes;
    int nodeCapacity;

    Element *tria3;
    int numTria3;
    int tria3Capacity;

    Element *quad4;
    int numQuad4;
    int quad4Capacity;

    Element *tetra;
    int numTetra;
    int tetraCapacity;

    Element *penta;
    int numPenta;
    int pentaCapacity;

    Element *hexa;
    int numHexa;
    int hexaCapacity;

    Element *pyram;
    int numPyram;
    int pyramCapacity;

    int *nodeMap;      /* Map from BDF node ID to sequential index */
    int maxNodeId;
} BdfMesh;

/*----------------------------------------------------------------------------*/
/* Helper functions                                                           */
/*----------------------------------------------------------------------------*/

static void initMesh(BdfMesh *mesh)
{
    memset(mesh, 0, sizeof(BdfMesh));
}

static void freeMesh(BdfMesh *mesh)
{
    if (mesh->nodes) free(mesh->nodes);
    if (mesh->tria3) free(mesh->tria3);
    if (mesh->quad4) free(mesh->quad4);
    if (mesh->tetra) free(mesh->tetra);
    if (mesh->penta) free(mesh->penta);
    if (mesh->hexa) free(mesh->hexa);
    if (mesh->pyram) free(mesh->pyram);
    if (mesh->nodeMap) free(mesh->nodeMap);
}

static void addNode(BdfMesh *mesh, int id, double x, double y, double z)
{
    if (mesh->numNodes >= mesh->nodeCapacity) {
        mesh->nodeCapacity = mesh->nodeCapacity ? mesh->nodeCapacity * 2 : 10000;
        mesh->nodes = realloc(mesh->nodes, mesh->nodeCapacity * sizeof(GridNode));
    }
    mesh->nodes[mesh->numNodes].id = id;
    mesh->nodes[mesh->numNodes].x = x;
    mesh->nodes[mesh->numNodes].y = y;
    mesh->nodes[mesh->numNodes].z = z;
    mesh->numNodes++;

    if (id > mesh->maxNodeId) mesh->maxNodeId = id;
}

static void addElement(Element **arr, int *count, int *capacity,
                       int id, int pid, int *nodes, int numNodes)
{
    if (*count >= *capacity) {
        *capacity = *capacity ? *capacity * 2 : 10000;
        *arr = realloc(*arr, *capacity * sizeof(Element));
    }
    (*arr)[*count].id = id;
    (*arr)[*count].pid = pid;
    for (int i = 0; i < numNodes; i++) {
        (*arr)[*count].nodes[i] = nodes[i];
    }
    (*count)++;
}

/*----------------------------------------------------------------------------*/
/* Parse Nastran field (8 or 16 character width)                              */
/*----------------------------------------------------------------------------*/

static double parseDouble(const char *field, int width)
{
    char buf[32];
    int len = 0;

    /* Copy field, trimming spaces */
    for (int i = 0; i < width && field[i]; i++) {
        if (!isspace((unsigned char)field[i])) {
            buf[len++] = field[i];
        }
    }
    buf[len] = '\0';

    if (len == 0) return 0.0;

    /* Handle Nastran's implicit exponent format (e.g., "1.5-3" means 1.5e-3) */
    for (int i = 1; i < len; i++) {
        if ((buf[i] == '-' || buf[i] == '+') &&
            buf[i-1] != 'e' && buf[i-1] != 'E' &&
            buf[i-1] != 'd' && buf[i-1] != 'D') {
            /* Insert 'E' before the sign */
            memmove(&buf[i+1], &buf[i], len - i + 1);
            buf[i] = 'E';
            len++;
            break;
        }
    }

    /* Replace D with E for Fortran-style exponents */
    for (int i = 0; i < len; i++) {
        if (buf[i] == 'd' || buf[i] == 'D') buf[i] = 'E';
    }

    return atof(buf);
}

static int parseInt(const char *field, int width)
{
    char buf[32];
    int len = 0;

    for (int i = 0; i < width && field[i]; i++) {
        if (!isspace((unsigned char)field[i])) {
            buf[len++] = field[i];
        }
    }
    buf[len] = '\0';

    if (len == 0) return 0;
    return atoi(buf);
}

/*----------------------------------------------------------------------------*/
/* Parse GRID entry (small field format - 8 char fields)                      */
/* GRID, ID, CP, X1, X2, X3, CD, PS, SEID                                     */
/*----------------------------------------------------------------------------*/

static void parseGridSmall(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    /* Skip CP field at line+16 */
    double x = parseDouble(line + 24, 8);
    double y = parseDouble(line + 32, 8);
    double z = parseDouble(line + 40, 8);

    addNode(mesh, id, x, y, z);
}

/*----------------------------------------------------------------------------*/
/* Parse GRID* entry (large field format - 16 char fields)                    */
/* Line 1: GRID*, ID, CP, X1, X2                                              */
/* Line 2: *, X3, CD, PS, SEID                                                */
/*----------------------------------------------------------------------------*/

static void parseGridLarge(BdfMesh *mesh, const char *line1, const char *line2)
{
    int id = parseInt(line1 + 8, 16);
    /* Skip CP field */
    double x = parseDouble(line1 + 40, 16);
    double y = parseDouble(line1 + 56, 16);
    double z = parseDouble(line2 + 8, 16);

    addNode(mesh, id, x, y, z);
}

/*----------------------------------------------------------------------------*/
/* Parse element entries                                                      */
/*----------------------------------------------------------------------------*/

static void parseCTRIA3(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[3];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);

    addElement(&mesh->tria3, &mesh->numTria3, &mesh->tria3Capacity,
               id, pid, nodes, 3);
}

static void parseCQUAD4(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[4];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);
    nodes[3] = parseInt(line + 48, 8);

    addElement(&mesh->quad4, &mesh->numQuad4, &mesh->quad4Capacity,
               id, pid, nodes, 4);
}

static void parseCTETRA(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[4];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);
    nodes[3] = parseInt(line + 48, 8);

    addElement(&mesh->tetra, &mesh->numTetra, &mesh->tetraCapacity,
               id, pid, nodes, 4);
}

static void parseCPENTA(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[6];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);
    nodes[3] = parseInt(line + 48, 8);
    nodes[4] = parseInt(line + 56, 8);
    nodes[5] = parseInt(line + 64, 8);

    addElement(&mesh->penta, &mesh->numPenta, &mesh->pentaCapacity,
               id, pid, nodes, 6);
}

static void parseCHEXA(BdfMesh *mesh, const char *line, const char *cont)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[8];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);
    nodes[3] = parseInt(line + 48, 8);
    nodes[4] = parseInt(line + 56, 8);
    nodes[5] = parseInt(line + 64, 8);

    /* Nodes 7 and 8 are on continuation line */
    if (cont) {
        nodes[6] = parseInt(cont + 8, 8);
        nodes[7] = parseInt(cont + 16, 8);
    } else {
        nodes[6] = 0;
        nodes[7] = 0;
    }

    addElement(&mesh->hexa, &mesh->numHexa, &mesh->hexaCapacity,
               id, pid, nodes, 8);
}

static void parseCPYRAM(BdfMesh *mesh, const char *line)
{
    int id = parseInt(line + 8, 8);
    int pid = parseInt(line + 16, 8);
    int nodes[5];
    nodes[0] = parseInt(line + 24, 8);
    nodes[1] = parseInt(line + 32, 8);
    nodes[2] = parseInt(line + 40, 8);
    nodes[3] = parseInt(line + 48, 8);
    nodes[4] = parseInt(line + 56, 8);

    addElement(&mesh->pyram, &mesh->numPyram, &mesh->pyramCapacity,
               id, pid, nodes, 5);
}

/*----------------------------------------------------------------------------*/
/* Build node ID to sequential index map                                      */
/*----------------------------------------------------------------------------*/

static void buildNodeMap(BdfMesh *mesh)
{
    mesh->nodeMap = calloc(mesh->maxNodeId + 1, sizeof(int));

    for (int i = 0; i < mesh->numNodes; i++) {
        mesh->nodeMap[mesh->nodes[i].id] = i + 1;  /* 1-based for meshb */
    }
}

/*----------------------------------------------------------------------------*/
/* Read BDF file                                                              */
/*----------------------------------------------------------------------------*/

static int readBDF(const char *filename, BdfMesh *mesh)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 0;
    }

    char line[256];
    char nextLine[256];
    int inBulk = 0;
    int lineNum = 0;

    printf("  Reading BDF file...\n");

    while (fgets(line, sizeof(line), f)) {
        lineNum++;

        /* Remove newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Pad line to at least 80 characters for field parsing */
        while (len < 80) {
            line[len++] = ' ';
        }
        line[80] = '\0';

        /* Skip comments */
        if (line[0] == '$') continue;

        /* Check for BEGIN BULK */
        if (strncasecmp(line, "BEGIN BULK", 10) == 0) {
            inBulk = 1;
            continue;
        }

        /* Check for ENDDATA */
        if (strncasecmp(line, "ENDDATA", 7) == 0) {
            break;
        }

        if (!inBulk) continue;

        /* Parse different card types */
        if (strncmp(line, "GRID*", 5) == 0) {
            /* Large field GRID - need continuation line */
            if (fgets(nextLine, sizeof(nextLine), f)) {
                lineNum++;
                len = strlen(nextLine);
                while (len > 0 && (nextLine[len-1] == '\n' || nextLine[len-1] == '\r')) {
                    nextLine[--len] = '\0';
                }
                while (len < 80) {
                    nextLine[len++] = ' ';
                }
                nextLine[80] = '\0';
                parseGridLarge(mesh, line, nextLine);
            }
        }
        else if (strncmp(line, "GRID", 4) == 0 && (line[4] == ' ' || line[4] == ',')) {
            parseGridSmall(mesh, line);
        }
        else if (strncmp(line, "CTRIA3", 6) == 0) {
            parseCTRIA3(mesh, line);
        }
        else if (strncmp(line, "CQUAD4", 6) == 0) {
            parseCQUAD4(mesh, line);
        }
        else if (strncmp(line, "CTETRA", 6) == 0) {
            parseCTETRA(mesh, line);
        }
        else if (strncmp(line, "CPENTA", 6) == 0) {
            parseCPENTA(mesh, line);
        }
        else if (strncmp(line, "CHEXA", 5) == 0) {
            /* CHEXA needs continuation for nodes 7-8 */
            if (fgets(nextLine, sizeof(nextLine), f)) {
                lineNum++;
                len = strlen(nextLine);
                while (len > 0 && (nextLine[len-1] == '\n' || nextLine[len-1] == '\r')) {
                    nextLine[--len] = '\0';
                }
                while (len < 80) {
                    nextLine[len++] = ' ';
                }
                nextLine[80] = '\0';
                parseCHEXA(mesh, line, nextLine);
            }
        }
        else if (strncmp(line, "CPYRAM", 6) == 0) {
            parseCPYRAM(mesh, line);
        }

        /* Progress indicator */
        if (lineNum % 100000 == 0) {
            printf("    Processed %d lines...\n", lineNum);
        }
    }

    fclose(f);

    printf("  Read %d lines\n", lineNum);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Write meshb file                                                           */
/*----------------------------------------------------------------------------*/

static int writeMeshb(const char *filename, BdfMesh *mesh)
{
    int ver = 2, dim = 3;
    int64_t outMsh;

    buildNodeMap(mesh);

    outMsh = GmfOpenMesh(filename, GmfWrite, ver, dim);
    if (outMsh <= 0) {
        fprintf(stderr, "Error: Cannot create output file %s\n", filename);
        return 0;
    }

    /* Write vertices */
    if (mesh->numNodes > 0) {
        GmfSetKwd(outMsh, GmfVertices, mesh->numNodes);
        for (int i = 0; i < mesh->numNodes; i++) {
            GmfSetLin(outMsh, GmfVertices,
                      mesh->nodes[i].x,
                      mesh->nodes[i].y,
                      mesh->nodes[i].z,
                      0);
        }
    }

    /* Write triangles */
    if (mesh->numTria3 > 0) {
        GmfSetKwd(outMsh, GmfTriangles, mesh->numTria3);
        for (int i = 0; i < mesh->numTria3; i++) {
            GmfSetLin(outMsh, GmfTriangles,
                      mesh->nodeMap[mesh->tria3[i].nodes[0]],
                      mesh->nodeMap[mesh->tria3[i].nodes[1]],
                      mesh->nodeMap[mesh->tria3[i].nodes[2]],
                      mesh->tria3[i].pid);
        }
    }

    /* Write quadrilaterals */
    if (mesh->numQuad4 > 0) {
        GmfSetKwd(outMsh, GmfQuadrilaterals, mesh->numQuad4);
        for (int i = 0; i < mesh->numQuad4; i++) {
            GmfSetLin(outMsh, GmfQuadrilaterals,
                      mesh->nodeMap[mesh->quad4[i].nodes[0]],
                      mesh->nodeMap[mesh->quad4[i].nodes[1]],
                      mesh->nodeMap[mesh->quad4[i].nodes[2]],
                      mesh->nodeMap[mesh->quad4[i].nodes[3]],
                      mesh->quad4[i].pid);
        }
    }

    /* Write tetrahedra */
    if (mesh->numTetra > 0) {
        GmfSetKwd(outMsh, GmfTetrahedra, mesh->numTetra);
        for (int i = 0; i < mesh->numTetra; i++) {
            GmfSetLin(outMsh, GmfTetrahedra,
                      mesh->nodeMap[mesh->tetra[i].nodes[0]],
                      mesh->nodeMap[mesh->tetra[i].nodes[1]],
                      mesh->nodeMap[mesh->tetra[i].nodes[2]],
                      mesh->nodeMap[mesh->tetra[i].nodes[3]],
                      mesh->tetra[i].pid);
        }
    }

    /* Write prisms */
    if (mesh->numPenta > 0) {
        GmfSetKwd(outMsh, GmfPrisms, mesh->numPenta);
        for (int i = 0; i < mesh->numPenta; i++) {
            GmfSetLin(outMsh, GmfPrisms,
                      mesh->nodeMap[mesh->penta[i].nodes[0]],
                      mesh->nodeMap[mesh->penta[i].nodes[1]],
                      mesh->nodeMap[mesh->penta[i].nodes[2]],
                      mesh->nodeMap[mesh->penta[i].nodes[3]],
                      mesh->nodeMap[mesh->penta[i].nodes[4]],
                      mesh->nodeMap[mesh->penta[i].nodes[5]],
                      mesh->penta[i].pid);
        }
    }

    /* Write hexahedra */
    if (mesh->numHexa > 0) {
        GmfSetKwd(outMsh, GmfHexahedra, mesh->numHexa);
        for (int i = 0; i < mesh->numHexa; i++) {
            GmfSetLin(outMsh, GmfHexahedra,
                      mesh->nodeMap[mesh->hexa[i].nodes[0]],
                      mesh->nodeMap[mesh->hexa[i].nodes[1]],
                      mesh->nodeMap[mesh->hexa[i].nodes[2]],
                      mesh->nodeMap[mesh->hexa[i].nodes[3]],
                      mesh->nodeMap[mesh->hexa[i].nodes[4]],
                      mesh->nodeMap[mesh->hexa[i].nodes[5]],
                      mesh->nodeMap[mesh->hexa[i].nodes[6]],
                      mesh->nodeMap[mesh->hexa[i].nodes[7]],
                      mesh->hexa[i].pid);
        }
    }

    /* Write pyramids */
    if (mesh->numPyram > 0) {
        GmfSetKwd(outMsh, GmfPyramids, mesh->numPyram);
        for (int i = 0; i < mesh->numPyram; i++) {
            GmfSetLin(outMsh, GmfPyramids,
                      mesh->nodeMap[mesh->pyram[i].nodes[0]],
                      mesh->nodeMap[mesh->pyram[i].nodes[1]],
                      mesh->nodeMap[mesh->pyram[i].nodes[2]],
                      mesh->nodeMap[mesh->pyram[i].nodes[3]],
                      mesh->nodeMap[mesh->pyram[i].nodes[4]],
                      mesh->pyram[i].pid);
        }
    }

    GmfCloseMesh(outMsh);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Main                                                                       */
/*----------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    char *inFile = NULL;
    char *outFile = NULL;
    char outBuf[1024];
    BdfMesh mesh;

    printf("\n");
    printf("  *************************************** \n");
    printf("  BDF2MESH Converter, version 1.0        \n");
    printf("  *************************************** \n");
    printf("\n");

    if (argc < 2) {
        printf("  Usage: bdf2mesh input.bdf [output.mesh[b]]\n");
        printf("\n");
        printf("  Converts Nastran BDF/NAS files to Gamma Mesh Format.\n");
        printf("  If output file is not specified, uses input name with .meshb extension.\n");
        printf("\n");
        printf("  Supported elements:\n");
        printf("    GRID, GRID*     - Nodes (small and large field format)\n");
        printf("    CTRIA3          - Triangles\n");
        printf("    CQUAD4          - Quadrilaterals\n");
        printf("    CTETRA          - Tetrahedra\n");
        printf("    CPENTA          - Prisms (wedges)\n");
        printf("    CHEXA           - Hexahedra\n");
        printf("    CPYRAM          - Pyramids\n");
        printf("\n");
        return 0;
    }

    inFile = argv[1];

    if (argc >= 3) {
        outFile = argv[2];
    } else {
        /* Generate output filename */
        strncpy(outBuf, inFile, sizeof(outBuf) - 10);
        outBuf[sizeof(outBuf) - 10] = '\0';

        /* Remove extension if present */
        char *ext = strrchr(outBuf, '.');
        if (ext && (strcasecmp(ext, ".bdf") == 0 ||
                    strcasecmp(ext, ".nas") == 0 ||
                    strcasecmp(ext, ".dat") == 0)) {
            *ext = '\0';
        }
        strcat(outBuf, ".meshb");
        outFile = outBuf;
    }

    printf("  Input:  %s\n", inFile);
    printf("  Output: %s\n", outFile);
    printf("\n");

    initMesh(&mesh);

    if (!readBDF(inFile, &mesh)) {
        freeMesh(&mesh);
        return 1;
    }

    printf("\n");
    printf("  Mesh statistics:\n");
    printf("    Vertices:      %d\n", mesh.numNodes);
    printf("    Triangles:     %d\n", mesh.numTria3);
    printf("    Quadrilaterals:%d\n", mesh.numQuad4);
    printf("    Tetrahedra:    %d\n", mesh.numTetra);
    printf("    Prisms:        %d\n", mesh.numPenta);
    printf("    Hexahedra:     %d\n", mesh.numHexa);
    printf("    Pyramids:      %d\n", mesh.numPyram);
    printf("\n");

    if (!writeMeshb(outFile, &mesh)) {
        freeMesh(&mesh);
        return 1;
    }

    printf("  Successfully wrote %s\n", outFile);
    printf("\n");

    freeMesh(&mesh);
    return 0;
}
