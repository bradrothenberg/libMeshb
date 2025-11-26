/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                     MESHB TO NASTRAN BDF CONVERTER                         */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Description:         Convert meshb/mesh files to Nastran BDF format        */
/* Author:              Claude / Anthropic                                    */
/* Creation date:       nov 25 2025                                           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <libmeshb7.h>

/*----------------------------------------------------------------------------*/
/* Windows compatibility                                                      */
/*----------------------------------------------------------------------------*/

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/*----------------------------------------------------------------------------*/
/* Write GRID entry in large field format                                     */
/*----------------------------------------------------------------------------*/

static void writeGridLarge(FILE *f, int id, double x, double y, double z)
{
    /* GRID* format: 8 + 16 + 16 + 16 + 16 = 72 chars on line 1 */
    /* Continuation: 8 + 16 + ... on line 2 */
    fprintf(f, "GRID*   %16d%16d%16.9E%16.9E\n", id, 0, x, y);
    fprintf(f, "*       %16.9E%16d\n", z, 0);
}

/*----------------------------------------------------------------------------*/
/* Write GRID entry in small field format                                     */
/*----------------------------------------------------------------------------*/

static void writeGridSmall(FILE *f, int id, double x, double y, double z)
{
    fprintf(f, "GRID    %8d%8d%8.4E%8.4E%8.4E\n", id, 0, x, y, z);
}

/*----------------------------------------------------------------------------*/
/* Write element entries                                                      */
/*----------------------------------------------------------------------------*/

static void writeCTRIA3(FILE *f, int id, int pid, int n1, int n2, int n3)
{
    fprintf(f, "CTRIA3  %8d%8d%8d%8d%8d\n", id, pid, n1, n2, n3);
}

static void writeCQUAD4(FILE *f, int id, int pid, int n1, int n2, int n3, int n4)
{
    fprintf(f, "CQUAD4  %8d%8d%8d%8d%8d%8d\n", id, pid, n1, n2, n3, n4);
}

static void writeCTETRA(FILE *f, int id, int pid, int n1, int n2, int n3, int n4)
{
    fprintf(f, "CTETRA  %8d%8d%8d%8d%8d%8d\n", id, pid, n1, n2, n3, n4);
}

static void writeCPENTA(FILE *f, int id, int pid, int *n)
{
    fprintf(f, "CPENTA  %8d%8d%8d%8d%8d%8d%8d%8d\n",
            id, pid, n[0], n[1], n[2], n[3], n[4], n[5]);
}

static void writeCHEXA(FILE *f, int id, int pid, int *n)
{
    fprintf(f, "CHEXA   %8d%8d%8d%8d%8d%8d%8d%8d\n",
            id, pid, n[0], n[1], n[2], n[3], n[4], n[5]);
    fprintf(f, "        %8d%8d\n", n[6], n[7]);
}

static void writeCPYRAM(FILE *f, int id, int pid, int *n)
{
    fprintf(f, "CPYRAM  %8d%8d%8d%8d%8d%8d%8d\n",
            id, pid, n[0], n[1], n[2], n[3], n[4]);
}

/*----------------------------------------------------------------------------*/
/* Main                                                                       */
/*----------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    char *inFile = NULL;
    char *outFile = NULL;
    char outBuf[1024];
    int largeField = 1;  /* Default to large field format for precision */
    int64_t inMsh;
    int ver, dim;
    FILE *f;

    printf("\n");
    printf("  *************************************** \n");
    printf("  MESH2BDF Converter, version 1.0        \n");
    printf("  *************************************** \n");
    printf("\n");

    if (argc < 2) {
        printf("  Usage: mesh2bdf input.mesh[b] [output.bdf] [-small]\n");
        printf("\n");
        printf("  Converts Gamma Mesh Format to Nastran BDF.\n");
        printf("  If output file is not specified, uses input name with .bdf extension.\n");
        printf("\n");
        printf("  Options:\n");
        printf("    -small    Use small field format (8-char fields, less precision)\n");
        printf("              Default is large field format (16-char fields)\n");
        printf("\n");
        printf("  Supported elements:\n");
        printf("    Vertices      -> GRID / GRID*\n");
        printf("    Triangles     -> CTRIA3\n");
        printf("    Quadrilaterals-> CQUAD4\n");
        printf("    Tetrahedra    -> CTETRA\n");
        printf("    Prisms        -> CPENTA\n");
        printf("    Hexahedra     -> CHEXA\n");
        printf("    Pyramids      -> CPYRAM\n");
        printf("\n");
        return 0;
    }

    /* Parse arguments */
    inFile = argv[1];
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-small") == 0) {
            largeField = 0;
        } else if (outFile == NULL) {
            outFile = argv[i];
        }
    }

    if (outFile == NULL) {
        /* Generate output filename */
        strncpy(outBuf, inFile, sizeof(outBuf) - 10);
        outBuf[sizeof(outBuf) - 10] = '\0';

        char *ext = strrchr(outBuf, '.');
        if (ext && (strcasecmp(ext, ".mesh") == 0 ||
                    strcasecmp(ext, ".meshb") == 0)) {
            *ext = '\0';
        }
        strcat(outBuf, ".bdf");
        outFile = outBuf;
    }

    printf("  Input:  %s\n", inFile);
    printf("  Output: %s (%s field format)\n", outFile, largeField ? "large" : "small");
    printf("\n");

    /* Open input mesh */
    inMsh = GmfOpenMesh(inFile, GmfRead, &ver, &dim);
    if (inMsh <= 0) {
        fprintf(stderr, "Error: Cannot open input file %s\n", inFile);
        return 1;
    }

    if (dim != 3) {
        fprintf(stderr, "Error: Only 3D meshes are supported\n");
        GmfCloseMesh(inMsh);
        return 1;
    }

    /* Get element counts */
    int numVer = (int)GmfStatKwd(inMsh, GmfVertices);
    int numTri = (int)GmfStatKwd(inMsh, GmfTriangles);
    int numQuad = (int)GmfStatKwd(inMsh, GmfQuadrilaterals);
    int numTet = (int)GmfStatKwd(inMsh, GmfTetrahedra);
    int numPrism = (int)GmfStatKwd(inMsh, GmfPrisms);
    int numHexa = (int)GmfStatKwd(inMsh, GmfHexahedra);
    int numPyram = (int)GmfStatKwd(inMsh, GmfPyramids);

    printf("  Mesh contents:\n");
    printf("    Vertices:       %d\n", numVer);
    printf("    Triangles:      %d\n", numTri);
    printf("    Quadrilaterals: %d\n", numQuad);
    printf("    Tetrahedra:     %d\n", numTet);
    printf("    Prisms:         %d\n", numPrism);
    printf("    Hexahedra:      %d\n", numHexa);
    printf("    Pyramids:       %d\n", numPyram);
    printf("\n");

    if (numVer == 0) {
        fprintf(stderr, "Error: No vertices in mesh\n");
        GmfCloseMesh(inMsh);
        return 1;
    }

    /* Open output file */
    f = fopen(outFile, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create output file %s\n", outFile);
        GmfCloseMesh(inMsh);
        return 1;
    }

    /* Write header */
    fprintf(f, "$ Nastran BDF exported from meshb\n");
    fprintf(f, "$ Vertices: %d\n", numVer);
    if (numTri > 0) fprintf(f, "$ Triangles: %d\n", numTri);
    if (numQuad > 0) fprintf(f, "$ Quadrilaterals: %d\n", numQuad);
    if (numTet > 0) fprintf(f, "$ Tetrahedra: %d\n", numTet);
    if (numPrism > 0) fprintf(f, "$ Prisms: %d\n", numPrism);
    if (numHexa > 0) fprintf(f, "$ Hexahedra: %d\n", numHexa);
    if (numPyram > 0) fprintf(f, "$ Pyramids: %d\n", numPyram);
    fprintf(f, "BEGIN BULK\n");

    /* Write vertices */
    fprintf(f, "$          Grid points\n");
    GmfGotoKwd(inMsh, GmfVertices);
    for (int i = 1; i <= numVer; i++) {
        double x, y, z;
        int ref;
        GmfGetLin(inMsh, GmfVertices, &x, &y, &z, &ref);

        if (largeField) {
            writeGridLarge(f, i, x, y, z);
        } else {
            writeGridSmall(f, i, x, y, z);
        }

        if (i % 100000 == 0) {
            printf("    Written %d vertices...\n", i);
        }
    }

    int elemId = 1;

    /* Write triangles */
    if (numTri > 0) {
        fprintf(f, "$          Triangles\n");
        GmfGotoKwd(inMsh, GmfTriangles);
        for (int i = 0; i < numTri; i++) {
            int n1, n2, n3, ref;
            GmfGetLin(inMsh, GmfTriangles, &n1, &n2, &n3, &ref);
            writeCTRIA3(f, elemId++, ref > 0 ? ref : 1, n1, n2, n3);
        }
        printf("  Written %d triangles\n", numTri);
    }

    /* Write quadrilaterals */
    if (numQuad > 0) {
        fprintf(f, "$          Quadrilaterals\n");
        GmfGotoKwd(inMsh, GmfQuadrilaterals);
        for (int i = 0; i < numQuad; i++) {
            int n1, n2, n3, n4, ref;
            GmfGetLin(inMsh, GmfQuadrilaterals, &n1, &n2, &n3, &n4, &ref);
            writeCQUAD4(f, elemId++, ref > 0 ? ref : 1, n1, n2, n3, n4);
        }
        printf("  Written %d quadrilaterals\n", numQuad);
    }

    /* Write tetrahedra */
    if (numTet > 0) {
        fprintf(f, "$          Tetrahedra\n");
        GmfGotoKwd(inMsh, GmfTetrahedra);
        for (int i = 0; i < numTet; i++) {
            int n1, n2, n3, n4, ref;
            GmfGetLin(inMsh, GmfTetrahedra, &n1, &n2, &n3, &n4, &ref);
            writeCTETRA(f, elemId++, ref > 0 ? ref : 1, n1, n2, n3, n4);

            if ((i + 1) % 100000 == 0) {
                printf("    Written %d tetrahedra...\n", i + 1);
            }
        }
        printf("  Written %d tetrahedra\n", numTet);
    }

    /* Write prisms */
    if (numPrism > 0) {
        fprintf(f, "$          Prisms\n");
        GmfGotoKwd(inMsh, GmfPrisms);
        for (int i = 0; i < numPrism; i++) {
            int n[6], ref;
            GmfGetLin(inMsh, GmfPrisms, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &ref);
            writeCPENTA(f, elemId++, ref > 0 ? ref : 1, n);
        }
        printf("  Written %d prisms\n", numPrism);
    }

    /* Write hexahedra */
    if (numHexa > 0) {
        fprintf(f, "$          Hexahedra\n");
        GmfGotoKwd(inMsh, GmfHexahedra);
        for (int i = 0; i < numHexa; i++) {
            int n[8], ref;
            GmfGetLin(inMsh, GmfHexahedra, &n[0], &n[1], &n[2], &n[3],
                      &n[4], &n[5], &n[6], &n[7], &ref);
            writeCHEXA(f, elemId++, ref > 0 ? ref : 1, n);
        }
        printf("  Written %d hexahedra\n", numHexa);
    }

    /* Write pyramids */
    if (numPyram > 0) {
        fprintf(f, "$          Pyramids\n");
        GmfGotoKwd(inMsh, GmfPyramids);
        for (int i = 0; i < numPyram; i++) {
            int n[5], ref;
            GmfGetLin(inMsh, GmfPyramids, &n[0], &n[1], &n[2], &n[3], &n[4], &ref);
            writeCPYRAM(f, elemId++, ref > 0 ? ref : 1, n);
        }
        printf("  Written %d pyramids\n", numPyram);
    }

    /* Write footer */
    fprintf(f, "ENDDATA\n");

    fclose(f);
    GmfCloseMesh(inMsh);

    printf("\n");
    printf("  Successfully wrote %s\n", outFile);
    printf("  Total elements: %d\n", elemId - 1);
    printf("\n");

    return 0;
}
