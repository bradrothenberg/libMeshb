/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                         STL TO MESHB CONVERTER                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Description:         Convert STL files (ASCII or binary) to meshb format   */
/* Author:              Claude / Anthropic                                    */
/* Creation date:       nov 25 2025                                           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <libmeshb7.h>

/*----------------------------------------------------------------------------*/
/* Windows compatibility                                                      */
/*----------------------------------------------------------------------------*/

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/*----------------------------------------------------------------------------*/
/* Structures                                                                 */
/*----------------------------------------------------------------------------*/

typedef struct {
    float x, y, z;
} Vec3f;

typedef struct {
    Vec3f normal;
    Vec3f v1, v2, v3;
} Triangle;

/*----------------------------------------------------------------------------*/
/* Vertex hash table for deduplication                                        */
/*----------------------------------------------------------------------------*/

#define HASH_SIZE 1000003
#define VERTEX_TOLERANCE 1e-9

typedef struct VertexNode {
    double x, y, z;
    int index;
    struct VertexNode *next;
} VertexNode;

static VertexNode *hashTable[HASH_SIZE];
static double *vertices = NULL;
static int numVertices = 0;
static int vertexCapacity = 0;

static unsigned int hashVertex(double x, double y, double z)
{
    /* Simple spatial hash */
    long long ix = (long long)(x * 1e6);
    long long iy = (long long)(y * 1e6);
    long long iz = (long long)(z * 1e6);
    unsigned long long h = (unsigned long long)ix * 73856093ULL ^
                           (unsigned long long)iy * 19349663ULL ^
                           (unsigned long long)iz * 83492791ULL;
    return (unsigned int)(h % HASH_SIZE);
}

static int addVertex(double x, double y, double z)
{
    unsigned int h = hashVertex(x, y, z);
    VertexNode *node = hashTable[h];

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
    if (numVertices >= vertexCapacity) {
        vertexCapacity = vertexCapacity ? vertexCapacity * 2 : 10000;
        vertices = realloc(vertices, vertexCapacity * 3 * sizeof(double));
        if (!vertices) {
            fprintf(stderr, "Error: Failed to allocate vertex memory\n");
            exit(1);
        }
    }

    int idx = numVertices;
    vertices[idx * 3 + 0] = x;
    vertices[idx * 3 + 1] = y;
    vertices[idx * 3 + 2] = z;
    numVertices++;

    /* Add to hash table */
    node = malloc(sizeof(VertexNode));
    if (!node) {
        fprintf(stderr, "Error: Failed to allocate hash node\n");
        exit(1);
    }
    node->x = x;
    node->y = y;
    node->z = z;
    node->index = idx;
    node->next = hashTable[h];
    hashTable[h] = node;

    return idx;
}

static void freeHashTable(void)
{
    for (int i = 0; i < HASH_SIZE; i++) {
        VertexNode *node = hashTable[i];
        while (node) {
            VertexNode *next = node->next;
            free(node);
            node = next;
        }
        hashTable[i] = NULL;
    }
}

/*----------------------------------------------------------------------------*/
/* Detect if STL file is binary or ASCII                                      */
/*----------------------------------------------------------------------------*/

static int isBinarySTL(const char *filename, long *fileSize)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    /* Get file size */
    fseek(f, 0, SEEK_END);
    *fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Check if file starts with "solid" (ASCII indicator) */
    char header[6] = {0};
    if (fread(header, 1, 5, f) != 5) {
        fclose(f);
        return -1;
    }

    /* If doesn't start with "solid", it's binary */
    if (strncmp(header, "solid", 5) != 0) {
        fclose(f);
        return 1;
    }

    /* Could be ASCII or binary with "solid" in header */
    /* Check expected binary size: 80 header + 4 count + 50 bytes per triangle */
    fseek(f, 80, SEEK_SET);
    uint32_t numTris;
    if (fread(&numTris, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return 0; /* Assume ASCII */
    }

    long expectedSize = 80 + 4 + (long)numTris * 50;
    fclose(f);

    /* If file size matches binary format exactly, it's binary */
    if (*fileSize == expectedSize) {
        return 1;
    }

    /* Otherwise assume ASCII */
    return 0;
}

/*----------------------------------------------------------------------------*/
/* Read binary STL file                                                       */
/*----------------------------------------------------------------------------*/

static int readBinarySTL(const char *filename, int **triangles, int *numTris)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 0;
    }

    /* Skip 80-byte header */
    fseek(f, 80, SEEK_SET);

    /* Read triangle count */
    uint32_t triCount;
    if (fread(&triCount, sizeof(uint32_t), 1, f) != 1) {
        fprintf(stderr, "Error: Cannot read triangle count\n");
        fclose(f);
        return 0;
    }

    printf("  Reading binary STL: %u triangles\n", triCount);

    *triangles = malloc(triCount * 3 * sizeof(int));
    if (!*triangles) {
        fprintf(stderr, "Error: Cannot allocate triangle memory\n");
        fclose(f);
        return 0;
    }

    /* Read triangles */
    for (uint32_t i = 0; i < triCount; i++) {
        float normal[3], v1[3], v2[3], v3[3];
        uint16_t attrByteCount;

        if (fread(normal, sizeof(float), 3, f) != 3 ||
            fread(v1, sizeof(float), 3, f) != 3 ||
            fread(v2, sizeof(float), 3, f) != 3 ||
            fread(v3, sizeof(float), 3, f) != 3 ||
            fread(&attrByteCount, sizeof(uint16_t), 1, f) != 1) {
            fprintf(stderr, "Error: Failed to read triangle %u\n", i);
            free(*triangles);
            fclose(f);
            return 0;
        }

        /* Add vertices and get indices (1-based for meshb) */
        (*triangles)[i * 3 + 0] = addVertex(v1[0], v1[1], v1[2]) + 1;
        (*triangles)[i * 3 + 1] = addVertex(v2[0], v2[1], v2[2]) + 1;
        (*triangles)[i * 3 + 2] = addVertex(v3[0], v3[1], v3[2]) + 1;
    }

    *numTris = (int)triCount;
    fclose(f);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Read ASCII STL file                                                        */
/*----------------------------------------------------------------------------*/

static int readAsciiSTL(const char *filename, int **triangles, int *numTris)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 0;
    }

    /* First pass: count triangles */
    char line[256];
    int triCount = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "facet normal")) {
            triCount++;
        }
    }

    printf("  Reading ASCII STL: %d triangles\n", triCount);

    if (triCount == 0) {
        fprintf(stderr, "Error: No triangles found in STL file\n");
        fclose(f);
        return 0;
    }

    *triangles = malloc(triCount * 3 * sizeof(int));
    if (!*triangles) {
        fprintf(stderr, "Error: Cannot allocate triangle memory\n");
        fclose(f);
        return 0;
    }

    /* Second pass: read triangles */
    rewind(f);
    int tri = 0;
    int vertInTri = 0;

    while (fgets(line, sizeof(line), f)) {
        float x, y, z;

        if (sscanf(line, " vertex %f %f %f", &x, &y, &z) == 3) {
            int idx = addVertex(x, y, z) + 1; /* 1-based for meshb */
            (*triangles)[tri * 3 + vertInTri] = idx;
            vertInTri++;

            if (vertInTri == 3) {
                tri++;
                vertInTri = 0;
            }
        }
    }

    *numTris = triCount;
    fclose(f);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Write meshb file                                                           */
/*----------------------------------------------------------------------------*/

static int writeMeshb(const char *filename, int *triangles, int numTris)
{
    int ver = 2, dim = 3;
    int64_t outMsh;

    outMsh = GmfOpenMesh(filename, GmfWrite, ver, dim);
    if (outMsh <= 0) {
        fprintf(stderr, "Error: Cannot create output file %s\n", filename);
        return 0;
    }

    /* Write vertices */
    GmfSetKwd(outMsh, GmfVertices, numVertices);
    for (int i = 0; i < numVertices; i++) {
        GmfSetLin(outMsh, GmfVertices,
                  vertices[i * 3 + 0],
                  vertices[i * 3 + 1],
                  vertices[i * 3 + 2],
                  0);
    }

    /* Write triangles */
    GmfSetKwd(outMsh, GmfTriangles, numTris);
    for (int i = 0; i < numTris; i++) {
        GmfSetLin(outMsh, GmfTriangles,
                  triangles[i * 3 + 0],
                  triangles[i * 3 + 1],
                  triangles[i * 3 + 2],
                  1); /* Reference/tag = 1 */
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
    int *triangles = NULL;
    int numTris = 0;

    printf("\n");
    printf("  *************************************** \n");
    printf("  STL2MESH Converter, version 1.0        \n");
    printf("  *************************************** \n");
    printf("\n");

    if (argc < 2) {
        printf("  Usage: stl2mesh input.stl [output.mesh[b]]\n");
        printf("\n");
        printf("  Converts STL files (ASCII or binary) to Gamma Mesh Format.\n");
        printf("  If output file is not specified, uses input name with .meshb extension.\n");
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

        /* Remove .stl extension if present */
        char *ext = strrchr(outBuf, '.');
        if (ext && (strcasecmp(ext, ".stl") == 0)) {
            *ext = '\0';
        }
        strcat(outBuf, ".meshb");
        outFile = outBuf;
    }

    printf("  Input:  %s\n", inFile);
    printf("  Output: %s\n", outFile);
    printf("\n");

    /* Initialize hash table */
    memset(hashTable, 0, sizeof(hashTable));

    /* Detect format and read */
    long fileSize;
    int isBinary = isBinarySTL(inFile, &fileSize);

    if (isBinary < 0) {
        fprintf(stderr, "Error: Cannot open input file %s\n", inFile);
        return 1;
    }

    printf("  File size: %ld bytes\n", fileSize);
    printf("  Format: %s\n", isBinary ? "Binary STL" : "ASCII STL");
    printf("\n");

    int success;
    if (isBinary) {
        success = readBinarySTL(inFile, &triangles, &numTris);
    } else {
        success = readAsciiSTL(inFile, &triangles, &numTris);
    }

    if (!success) {
        freeHashTable();
        if (vertices) free(vertices);
        return 1;
    }

    printf("\n");
    printf("  Mesh statistics:\n");
    printf("    Unique vertices: %d\n", numVertices);
    printf("    Triangles:       %d\n", numTris);
    printf("\n");

    /* Write output */
    if (!writeMeshb(outFile, triangles, numTris)) {
        freeHashTable();
        if (vertices) free(vertices);
        if (triangles) free(triangles);
        return 1;
    }

    printf("  Successfully wrote %s\n", outFile);
    printf("\n");

    /* Cleanup */
    freeHashTable();
    if (vertices) free(vertices);
    if (triangles) free(triangles);

    return 0;
}
