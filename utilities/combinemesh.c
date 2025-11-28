/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                     COMBINE BDF + STL TO MESHB                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Description:         Combine a BDF volume mesh and STL surface mesh        */
/*                      into a single meshb file                              */
/* Author:              Claude / Anthropic                                    */
/* Creation date:       nov 25 2025                                           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

#include <libmeshb7.h>

/*----------------------------------------------------------------------------*/
/* Windows compatibility                                                      */
/*----------------------------------------------------------------------------*/

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

/*----------------------------------------------------------------------------*/
/* Constants                                                                  */
/*----------------------------------------------------------------------------*/

#define HASH_SIZE 1000003
#define VERTEX_TOLERANCE 1e-9

/*----------------------------------------------------------------------------*/
/* Data structures                                                            */
/*----------------------------------------------------------------------------*/

typedef struct {
    double x, y, z;
    int ref;
} Vertex;

typedef struct {
    int nodes[8];
    int ref;
} Element;

typedef struct VertexNode {
    double x, y, z;
    int index;
    struct VertexNode *next;
} VertexNode;

typedef struct {
    Vertex *vertices;
    int numVertices;
    int vertexCapacity;

    Element *triangles;
    int numTriangles;
    int triangleCapacity;

    Element *quads;
    int numQuads;
    int quadCapacity;

    Element *tetra;
    int numTetra;
    int tetraCapacity;

    Element *prisms;
    int numPrisms;
    int prismCapacity;

    Element *hexa;
    int numHexa;
    int hexaCapacity;

    Element *pyramids;
    int numPyramids;
    int pyramidCapacity;

    /* For vertex merging */
    VertexNode *hashTable[HASH_SIZE];
    int *bdfNodeMap;
    int maxBdfNodeId;
} CombinedMesh;

/*----------------------------------------------------------------------------*/
/* Hash table functions for vertex deduplication                              */
/*----------------------------------------------------------------------------*/

static unsigned int hashVertex(double x, double y, double z)
{
    long long ix = (long long)(x * 1e6);
    long long iy = (long long)(y * 1e6);
    long long iz = (long long)(z * 1e6);
    unsigned long long h = (unsigned long long)ix * 73856093ULL ^
                           (unsigned long long)iy * 19349663ULL ^
                           (unsigned long long)iz * 83492791ULL;
    return (unsigned int)(h % HASH_SIZE);
}

static int findOrAddVertex(CombinedMesh *mesh, double x, double y, double z, int ref)
{
    unsigned int h = hashVertex(x, y, z);
    VertexNode *node = mesh->hashTable[h];

    /* Search for existing vertex */
    while (node) {
        if (fabs(node->x - x) < VERTEX_TOLERANCE &&
            fabs(node->y - y) < VERTEX_TOLERANCE &&
            fabs(node->z - z) < VERTEX_TOLERANCE) {
            return node->index;
        }
        node = node->next;
    }

    /* Add new vertex */
    if (mesh->numVertices >= mesh->vertexCapacity) {
        mesh->vertexCapacity = mesh->vertexCapacity ? mesh->vertexCapacity * 2 : 10000;
        mesh->vertices = realloc(mesh->vertices, mesh->vertexCapacity * sizeof(Vertex));
    }

    int idx = mesh->numVertices;
    mesh->vertices[idx].x = x;
    mesh->vertices[idx].y = y;
    mesh->vertices[idx].z = z;
    mesh->vertices[idx].ref = ref;
    mesh->numVertices++;

    /* Add to hash table */
    node = malloc(sizeof(VertexNode));
    node->x = x;
    node->y = y;
    node->z = z;
    node->index = idx;
    node->next = mesh->hashTable[h];
    mesh->hashTable[h] = node;

    return idx;
}

static void freeHashTable(CombinedMesh *mesh)
{
    for (int i = 0; i < HASH_SIZE; i++) {
        VertexNode *node = mesh->hashTable[i];
        while (node) {
            VertexNode *next = node->next;
            free(node);
            node = next;
        }
        mesh->hashTable[i] = NULL;
    }
}

/*----------------------------------------------------------------------------*/
/* Initialize/free mesh                                                       */
/*----------------------------------------------------------------------------*/

static void initMesh(CombinedMesh *mesh)
{
    memset(mesh, 0, sizeof(CombinedMesh));
}

static void freeMesh(CombinedMesh *mesh)
{
    if (mesh->vertices) free(mesh->vertices);
    if (mesh->triangles) free(mesh->triangles);
    if (mesh->quads) free(mesh->quads);
    if (mesh->tetra) free(mesh->tetra);
    if (mesh->prisms) free(mesh->prisms);
    if (mesh->hexa) free(mesh->hexa);
    if (mesh->pyramids) free(mesh->pyramids);
    if (mesh->bdfNodeMap) free(mesh->bdfNodeMap);
    freeHashTable(mesh);
}

/*----------------------------------------------------------------------------*/
/* BDF parsing helpers                                                        */
/*----------------------------------------------------------------------------*/

static double parseDouble(const char *field, int width)
{
    char buf[32];
    int len = 0;

    for (int i = 0; i < width && field[i]; i++) {
        if (!isspace((unsigned char)field[i])) {
            buf[len++] = field[i];
        }
    }
    buf[len] = '\0';

    if (len == 0) return 0.0;

    /* Handle Nastran's implicit exponent format */
    for (int i = 1; i < len; i++) {
        if ((buf[i] == '-' || buf[i] == '+') &&
            buf[i-1] != 'e' && buf[i-1] != 'E' &&
            buf[i-1] != 'd' && buf[i-1] != 'D') {
            memmove(&buf[i+1], &buf[i], len - i + 1);
            buf[i] = 'E';
            len++;
            break;
        }
    }

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
/* Read BDF file                                                              */
/*----------------------------------------------------------------------------*/

static int readBDF(const char *filename, CombinedMesh *mesh, int surfaceRef)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open BDF file %s\n", filename);
        return 0;
    }

    char line[256], nextLine[256];
    int inBulk = 0;
    int lineNum = 0;

    /* First pass: read vertices and build node map */
    printf("  Reading BDF vertices...\n");

    while (fgets(line, sizeof(line), f)) {
        lineNum++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        while (len < 80) line[len++] = ' ';
        line[80] = '\0';

        if (line[0] == '$') continue;
        if (strncasecmp(line, "BEGIN BULK", 10) == 0) { inBulk = 1; continue; }
        if (strncasecmp(line, "ENDDATA", 7) == 0) break;
        if (!inBulk) continue;

        if (strncmp(line, "GRID*", 5) == 0) {
            if (fgets(nextLine, sizeof(nextLine), f)) {
                lineNum++;
                len = strlen(nextLine);
                while (len > 0 && (nextLine[len-1] == '\n' || nextLine[len-1] == '\r'))
                    nextLine[--len] = '\0';
                while (len < 80) nextLine[len++] = ' ';
                nextLine[80] = '\0';

                int id = parseInt(line + 8, 16);
                double x = parseDouble(line + 40, 16);
                double y = parseDouble(line + 56, 16);
                double z = parseDouble(nextLine + 8, 16);

                if (id > mesh->maxBdfNodeId) mesh->maxBdfNodeId = id;
                findOrAddVertex(mesh, x, y, z, 0);
            }
        }
        else if (strncmp(line, "GRID", 4) == 0 && (line[4] == ' ' || line[4] == ',')) {
            int id = parseInt(line + 8, 8);
            double x = parseDouble(line + 24, 8);
            double y = parseDouble(line + 32, 8);
            double z = parseDouble(line + 40, 8);

            if (id > mesh->maxBdfNodeId) mesh->maxBdfNodeId = id;
            findOrAddVertex(mesh, x, y, z, 0);
        }

        if (lineNum % 100000 == 0) printf("    Processed %d lines...\n", lineNum);
    }

    /* Build BDF node ID to index map */
    mesh->bdfNodeMap = calloc(mesh->maxBdfNodeId + 1, sizeof(int));

    /* Second pass: rebuild vertex map and read elements */
    rewind(f);
    inBulk = 0;
    lineNum = 0;
    int vertexIdx = 0;

    printf("  Building node map and reading elements...\n");

    while (fgets(line, sizeof(line), f)) {
        lineNum++;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        while (len < 80) line[len++] = ' ';
        line[80] = '\0';

        if (line[0] == '$') continue;
        if (strncasecmp(line, "BEGIN BULK", 10) == 0) { inBulk = 1; continue; }
        if (strncasecmp(line, "ENDDATA", 7) == 0) break;
        if (!inBulk) continue;

        if (strncmp(line, "GRID*", 5) == 0) {
            if (fgets(nextLine, sizeof(nextLine), f)) {
                lineNum++;
                int id = parseInt(line + 8, 16);
                mesh->bdfNodeMap[id] = vertexIdx + 1;  /* 1-based */
                vertexIdx++;
            }
        }
        else if (strncmp(line, "GRID", 4) == 0 && (line[4] == ' ' || line[4] == ',')) {
            int id = parseInt(line + 8, 8);
            mesh->bdfNodeMap[id] = vertexIdx + 1;
            vertexIdx++;
        }
        else if (strncmp(line, "CTRIA3", 6) == 0) {
            if (mesh->numTriangles >= mesh->triangleCapacity) {
                mesh->triangleCapacity = mesh->triangleCapacity ? mesh->triangleCapacity * 2 : 10000;
                mesh->triangles = realloc(mesh->triangles, mesh->triangleCapacity * sizeof(Element));
            }
            int pid = parseInt(line + 16, 8);
            mesh->triangles[mesh->numTriangles].nodes[0] = mesh->bdfNodeMap[parseInt(line + 24, 8)];
            mesh->triangles[mesh->numTriangles].nodes[1] = mesh->bdfNodeMap[parseInt(line + 32, 8)];
            mesh->triangles[mesh->numTriangles].nodes[2] = mesh->bdfNodeMap[parseInt(line + 40, 8)];
            mesh->triangles[mesh->numTriangles].ref = pid > 0 ? pid : surfaceRef;
            mesh->numTriangles++;
        }
        else if (strncmp(line, "CQUAD4", 6) == 0) {
            if (mesh->numQuads >= mesh->quadCapacity) {
                mesh->quadCapacity = mesh->quadCapacity ? mesh->quadCapacity * 2 : 10000;
                mesh->quads = realloc(mesh->quads, mesh->quadCapacity * sizeof(Element));
            }
            int pid = parseInt(line + 16, 8);
            mesh->quads[mesh->numQuads].nodes[0] = mesh->bdfNodeMap[parseInt(line + 24, 8)];
            mesh->quads[mesh->numQuads].nodes[1] = mesh->bdfNodeMap[parseInt(line + 32, 8)];
            mesh->quads[mesh->numQuads].nodes[2] = mesh->bdfNodeMap[parseInt(line + 40, 8)];
            mesh->quads[mesh->numQuads].nodes[3] = mesh->bdfNodeMap[parseInt(line + 48, 8)];
            mesh->quads[mesh->numQuads].ref = pid > 0 ? pid : surfaceRef;
            mesh->numQuads++;
        }
        else if (strncmp(line, "CTETRA", 6) == 0) {
            if (mesh->numTetra >= mesh->tetraCapacity) {
                mesh->tetraCapacity = mesh->tetraCapacity ? mesh->tetraCapacity * 2 : 10000;
                mesh->tetra = realloc(mesh->tetra, mesh->tetraCapacity * sizeof(Element));
            }
            int pid = parseInt(line + 16, 8);
            mesh->tetra[mesh->numTetra].nodes[0] = mesh->bdfNodeMap[parseInt(line + 24, 8)];
            mesh->tetra[mesh->numTetra].nodes[1] = mesh->bdfNodeMap[parseInt(line + 32, 8)];
            mesh->tetra[mesh->numTetra].nodes[2] = mesh->bdfNodeMap[parseInt(line + 40, 8)];
            mesh->tetra[mesh->numTetra].nodes[3] = mesh->bdfNodeMap[parseInt(line + 48, 8)];
            mesh->tetra[mesh->numTetra].ref = pid > 0 ? pid : 1;
            mesh->numTetra++;
        }
        else if (strncmp(line, "CPENTA", 6) == 0) {
            if (mesh->numPrisms >= mesh->prismCapacity) {
                mesh->prismCapacity = mesh->prismCapacity ? mesh->prismCapacity * 2 : 1000;
                mesh->prisms = realloc(mesh->prisms, mesh->prismCapacity * sizeof(Element));
            }
            int pid = parseInt(line + 16, 8);
            for (int i = 0; i < 6; i++)
                mesh->prisms[mesh->numPrisms].nodes[i] = mesh->bdfNodeMap[parseInt(line + 24 + i*8, 8)];
            mesh->prisms[mesh->numPrisms].ref = pid > 0 ? pid : 1;
            mesh->numPrisms++;
        }
        else if (strncmp(line, "CHEXA", 5) == 0) {
            if (fgets(nextLine, sizeof(nextLine), f)) {
                lineNum++;
                len = strlen(nextLine);
                while (len > 0 && (nextLine[len-1] == '\n' || nextLine[len-1] == '\r'))
                    nextLine[--len] = '\0';
                while (len < 80) nextLine[len++] = ' ';
                nextLine[80] = '\0';

                if (mesh->numHexa >= mesh->hexaCapacity) {
                    mesh->hexaCapacity = mesh->hexaCapacity ? mesh->hexaCapacity * 2 : 1000;
                    mesh->hexa = realloc(mesh->hexa, mesh->hexaCapacity * sizeof(Element));
                }
                int pid = parseInt(line + 16, 8);
                for (int i = 0; i < 6; i++)
                    mesh->hexa[mesh->numHexa].nodes[i] = mesh->bdfNodeMap[parseInt(line + 24 + i*8, 8)];
                mesh->hexa[mesh->numHexa].nodes[6] = mesh->bdfNodeMap[parseInt(nextLine + 8, 8)];
                mesh->hexa[mesh->numHexa].nodes[7] = mesh->bdfNodeMap[parseInt(nextLine + 16, 8)];
                mesh->hexa[mesh->numHexa].ref = pid > 0 ? pid : 1;
                mesh->numHexa++;
            }
        }
        else if (strncmp(line, "CPYRAM", 6) == 0) {
            if (mesh->numPyramids >= mesh->pyramidCapacity) {
                mesh->pyramidCapacity = mesh->pyramidCapacity ? mesh->pyramidCapacity * 2 : 1000;
                mesh->pyramids = realloc(mesh->pyramids, mesh->pyramidCapacity * sizeof(Element));
            }
            int pid = parseInt(line + 16, 8);
            for (int i = 0; i < 5; i++)
                mesh->pyramids[mesh->numPyramids].nodes[i] = mesh->bdfNodeMap[parseInt(line + 24 + i*8, 8)];
            mesh->pyramids[mesh->numPyramids].ref = pid > 0 ? pid : 1;
            mesh->numPyramids++;
        }

        if (lineNum % 100000 == 0) printf("    Processed %d lines...\n", lineNum);
    }

    fclose(f);
    printf("  BDF: %d vertices, %d tets, %d tris, %d quads\n",
           mesh->numVertices, mesh->numTetra, mesh->numTriangles, mesh->numQuads);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Read STL file and merge vertices                                           */
/*----------------------------------------------------------------------------*/

static int isBinarySTL(const char *filename, long *fileSize)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    *fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[6] = {0};
    if (fread(header, 1, 5, f) != 5) { fclose(f); return -1; }

    if (strncmp(header, "solid", 5) != 0) { fclose(f); return 1; }

    fseek(f, 80, SEEK_SET);
    uint32_t numTris;
    if (fread(&numTris, sizeof(uint32_t), 1, f) != 1) { fclose(f); return 0; }

    long expectedSize = 80 + 4 + (long)numTris * 50;
    fclose(f);

    return (*fileSize == expectedSize) ? 1 : 0;
}

static int readSTL(const char *filename, CombinedMesh *mesh, int surfaceRef)
{
    long fileSize;
    int isBinary = isBinarySTL(filename, &fileSize);

    if (isBinary < 0) {
        fprintf(stderr, "Error: Cannot open STL file %s\n", filename);
        return 0;
    }

    int stlTriCount = 0;
    int stlVertsMerged = 0;
    int stlVertsNew = 0;
    int prevNumVerts = mesh->numVertices;

    if (isBinary) {
        printf("  Reading binary STL...\n");
        FILE *f = fopen(filename, "rb");
        fseek(f, 80, SEEK_SET);

        uint32_t triCount;
        fread(&triCount, sizeof(uint32_t), 1, f);

        for (uint32_t i = 0; i < triCount; i++) {
            float normal[3], v1[3], v2[3], v3[3];
            uint16_t attr;

            fread(normal, sizeof(float), 3, f);
            fread(v1, sizeof(float), 3, f);
            fread(v2, sizeof(float), 3, f);
            fread(v3, sizeof(float), 3, f);
            fread(&attr, sizeof(uint16_t), 1, f);

            if (mesh->numTriangles >= mesh->triangleCapacity) {
                mesh->triangleCapacity = mesh->triangleCapacity ? mesh->triangleCapacity * 2 : 10000;
                mesh->triangles = realloc(mesh->triangles, mesh->triangleCapacity * sizeof(Element));
            }

            int prevNum = mesh->numVertices;
            mesh->triangles[mesh->numTriangles].nodes[0] = findOrAddVertex(mesh, v1[0], v1[1], v1[2], 0) + 1;
            if (mesh->numVertices > prevNum) stlVertsNew++; else stlVertsMerged++;

            prevNum = mesh->numVertices;
            mesh->triangles[mesh->numTriangles].nodes[1] = findOrAddVertex(mesh, v2[0], v2[1], v2[2], 0) + 1;
            if (mesh->numVertices > prevNum) stlVertsNew++; else stlVertsMerged++;

            prevNum = mesh->numVertices;
            mesh->triangles[mesh->numTriangles].nodes[2] = findOrAddVertex(mesh, v3[0], v3[1], v3[2], 0) + 1;
            if (mesh->numVertices > prevNum) stlVertsNew++; else stlVertsMerged++;

            mesh->triangles[mesh->numTriangles].ref = surfaceRef;
            mesh->numTriangles++;
            stlTriCount++;
        }
        fclose(f);
    }
    else {
        printf("  Reading ASCII STL...\n");
        FILE *f = fopen(filename, "r");
        char line[256];
        float verts[3][3];
        int vertIdx = 0;

        while (fgets(line, sizeof(line), f)) {
            float x, y, z;
            if (sscanf(line, " vertex %f %f %f", &x, &y, &z) == 3) {
                verts[vertIdx][0] = x;
                verts[vertIdx][1] = y;
                verts[vertIdx][2] = z;
                vertIdx++;

                if (vertIdx == 3) {
                    if (mesh->numTriangles >= mesh->triangleCapacity) {
                        mesh->triangleCapacity = mesh->triangleCapacity ? mesh->triangleCapacity * 2 : 10000;
                        mesh->triangles = realloc(mesh->triangles, mesh->triangleCapacity * sizeof(Element));
                    }

                    for (int i = 0; i < 3; i++) {
                        int prevNum = mesh->numVertices;
                        mesh->triangles[mesh->numTriangles].nodes[i] =
                            findOrAddVertex(mesh, verts[i][0], verts[i][1], verts[i][2], 0) + 1;
                        if (mesh->numVertices > prevNum) stlVertsNew++; else stlVertsMerged++;
                    }
                    mesh->triangles[mesh->numTriangles].ref = surfaceRef;
                    mesh->numTriangles++;
                    stlTriCount++;
                    vertIdx = 0;
                }
            }
        }
        fclose(f);
    }

    printf("  STL: %d triangles, %d new vertices, %d vertices merged with BDF\n",
           stlTriCount, stlVertsNew, stlVertsMerged);

    return 1;
}

/*----------------------------------------------------------------------------*/
/* Write combined meshb                                                       */
/*----------------------------------------------------------------------------*/

static int writeMeshb(const char *filename, CombinedMesh *mesh)
{
    int ver = 2, dim = 3;
    int64_t outMsh = GmfOpenMesh(filename, GmfWrite, ver, dim);

    if (outMsh <= 0) {
        fprintf(stderr, "Error: Cannot create output file %s\n", filename);
        return 0;
    }

    /* Write vertices */
    GmfSetKwd(outMsh, GmfVertices, mesh->numVertices);
    for (int i = 0; i < mesh->numVertices; i++) {
        GmfSetLin(outMsh, GmfVertices,
                  mesh->vertices[i].x,
                  mesh->vertices[i].y,
                  mesh->vertices[i].z,
                  mesh->vertices[i].ref);
    }

    /* Write triangles */
    if (mesh->numTriangles > 0) {
        GmfSetKwd(outMsh, GmfTriangles, mesh->numTriangles);
        for (int i = 0; i < mesh->numTriangles; i++) {
            GmfSetLin(outMsh, GmfTriangles,
                      mesh->triangles[i].nodes[0],
                      mesh->triangles[i].nodes[1],
                      mesh->triangles[i].nodes[2],
                      mesh->triangles[i].ref);
        }
    }

    /* Write quads */
    if (mesh->numQuads > 0) {
        GmfSetKwd(outMsh, GmfQuadrilaterals, mesh->numQuads);
        for (int i = 0; i < mesh->numQuads; i++) {
            GmfSetLin(outMsh, GmfQuadrilaterals,
                      mesh->quads[i].nodes[0],
                      mesh->quads[i].nodes[1],
                      mesh->quads[i].nodes[2],
                      mesh->quads[i].nodes[3],
                      mesh->quads[i].ref);
        }
    }

    /* Write tetrahedra */
    if (mesh->numTetra > 0) {
        GmfSetKwd(outMsh, GmfTetrahedra, mesh->numTetra);
        for (int i = 0; i < mesh->numTetra; i++) {
            GmfSetLin(outMsh, GmfTetrahedra,
                      mesh->tetra[i].nodes[0],
                      mesh->tetra[i].nodes[1],
                      mesh->tetra[i].nodes[2],
                      mesh->tetra[i].nodes[3],
                      mesh->tetra[i].ref);
        }
    }

    /* Write prisms */
    if (mesh->numPrisms > 0) {
        GmfSetKwd(outMsh, GmfPrisms, mesh->numPrisms);
        for (int i = 0; i < mesh->numPrisms; i++) {
            GmfSetLin(outMsh, GmfPrisms,
                      mesh->prisms[i].nodes[0],
                      mesh->prisms[i].nodes[1],
                      mesh->prisms[i].nodes[2],
                      mesh->prisms[i].nodes[3],
                      mesh->prisms[i].nodes[4],
                      mesh->prisms[i].nodes[5],
                      mesh->prisms[i].ref);
        }
    }

    /* Write hexahedra */
    if (mesh->numHexa > 0) {
        GmfSetKwd(outMsh, GmfHexahedra, mesh->numHexa);
        for (int i = 0; i < mesh->numHexa; i++) {
            GmfSetLin(outMsh, GmfHexahedra,
                      mesh->hexa[i].nodes[0],
                      mesh->hexa[i].nodes[1],
                      mesh->hexa[i].nodes[2],
                      mesh->hexa[i].nodes[3],
                      mesh->hexa[i].nodes[4],
                      mesh->hexa[i].nodes[5],
                      mesh->hexa[i].nodes[6],
                      mesh->hexa[i].nodes[7],
                      mesh->hexa[i].ref);
        }
    }

    /* Write pyramids */
    if (mesh->numPyramids > 0) {
        GmfSetKwd(outMsh, GmfPyramids, mesh->numPyramids);
        for (int i = 0; i < mesh->numPyramids; i++) {
            GmfSetLin(outMsh, GmfPyramids,
                      mesh->pyramids[i].nodes[0],
                      mesh->pyramids[i].nodes[1],
                      mesh->pyramids[i].nodes[2],
                      mesh->pyramids[i].nodes[3],
                      mesh->pyramids[i].nodes[4],
                      mesh->pyramids[i].ref);
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
    char *bdfFile = NULL;
    char *stlFile = NULL;
    char *outFile = NULL;
    char outBuf[1024];
    int stlRef = 2;  /* Default reference for STL triangles */
    CombinedMesh *mesh;

    printf("\n");
    printf("  *************************************** \n");
    printf("  COMBINEMESH - BDF + STL to MESHB        \n");
    printf("  *************************************** \n");
    printf("\n");

    if (argc < 3) {
        printf("  Usage: combinemesh volume.bdf surface.stl [output.meshb] [-ref N]\n");
        printf("\n");
        printf("  Combines a Nastran BDF volume mesh with an STL surface mesh.\n");
        printf("  Vertices from the STL that match BDF vertices are merged.\n");
        printf("\n");
        printf("  Options:\n");
        printf("    -ref N    Set reference/tag for STL triangles (default: 2)\n");
        printf("\n");
        printf("  The BDF file provides the volume mesh (tetrahedra, etc.)\n");
        printf("  The STL file provides additional surface triangles.\n");
        printf("\n");
        return 0;
    }

    /* Allocate mesh on heap - hash table is too large for stack */
    mesh = calloc(1, sizeof(CombinedMesh));
    if (!mesh) {
        fprintf(stderr, "Error: Cannot allocate memory for mesh\n");
        return 1;
    }

    /* Parse arguments */
    bdfFile = argv[1];
    stlFile = argv[2];

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-ref") == 0 && i + 1 < argc) {
            stlRef = atoi(argv[++i]);
        } else if (outFile == NULL) {
            outFile = argv[i];
        }
    }

    if (outFile == NULL) {
        /* Generate output filename from BDF file */
        strncpy(outBuf, bdfFile, sizeof(outBuf) - 20);
        outBuf[sizeof(outBuf) - 20] = '\0';
        char *ext = strrchr(outBuf, '.');
        if (ext) *ext = '\0';
        strcat(outBuf, "_combined.meshb");
        outFile = outBuf;
    }

    printf("  BDF input:  %s\n", bdfFile);
    printf("  STL input:  %s\n", stlFile);
    printf("  Output:     %s\n", outFile);
    printf("  STL ref:    %d\n", stlRef);
    printf("\n");

    /* Read BDF first (establishes vertex base) */
    if (!readBDF(bdfFile, mesh, stlRef)) {
        freeMesh(mesh);
        free(mesh);
        return 1;
    }

    printf("\n");

    /* Read STL and merge vertices */
    if (!readSTL(stlFile, mesh, stlRef)) {
        freeMesh(mesh);
        free(mesh);
        return 1;
    }

    printf("\n");
    printf("  Combined mesh statistics:\n");
    printf("    Vertices:       %d\n", mesh->numVertices);
    printf("    Triangles:      %d\n", mesh->numTriangles);
    printf("    Quadrilaterals: %d\n", mesh->numQuads);
    printf("    Tetrahedra:     %d\n", mesh->numTetra);
    printf("    Prisms:         %d\n", mesh->numPrisms);
    printf("    Hexahedra:      %d\n", mesh->numHexa);
    printf("    Pyramids:       %d\n", mesh->numPyramids);
    printf("\n");

    /* Write output */
    if (!writeMeshb(outFile, mesh)) {
        freeMesh(mesh);
        free(mesh);
        return 1;
    }

    printf("  Successfully wrote %s\n", outFile);
    printf("\n");

    freeMesh(mesh);
    free(mesh);
    return 0;
}
