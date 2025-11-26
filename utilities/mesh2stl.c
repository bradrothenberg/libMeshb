/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                     MESHB TO STL CONVERTER                                 */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/* Description:         Convert meshb/mesh files to STL format                */
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
/* Compute triangle normal                                                    */
/*----------------------------------------------------------------------------*/

static void computeNormal(double *v1, double *v2, double *v3, float *normal)
{
    double e1[3], e2[3], n[3], len;

    /* Edge vectors */
    e1[0] = v2[0] - v1[0];
    e1[1] = v2[1] - v1[1];
    e1[2] = v2[2] - v1[2];

    e2[0] = v3[0] - v1[0];
    e2[1] = v3[1] - v1[1];
    e2[2] = v3[2] - v1[2];

    /* Cross product */
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];

    /* Normalize */
    len = sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (len > 1e-12) {
        normal[0] = (float)(n[0] / len);
        normal[1] = (float)(n[1] / len);
        normal[2] = (float)(n[2] / len);
    } else {
        normal[0] = 0.0f;
        normal[1] = 0.0f;
        normal[2] = 1.0f;
    }
}

/*----------------------------------------------------------------------------*/
/* Write ASCII STL                                                            */
/*----------------------------------------------------------------------------*/

static int writeAsciiSTL(const char *filename, double (*vertices)[3],
                         int (*triangles)[3], int numTri)
{
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create file %s\n", filename);
        return 0;
    }

    fprintf(f, "solid mesh\n");

    for (int i = 0; i < numTri; i++) {
        float normal[3];
        double *v1 = vertices[triangles[i][0]];
        double *v2 = vertices[triangles[i][1]];
        double *v3 = vertices[triangles[i][2]];

        computeNormal(v1, v2, v3, normal);

        fprintf(f, "  facet normal %e %e %e\n", normal[0], normal[1], normal[2]);
        fprintf(f, "    outer loop\n");
        fprintf(f, "      vertex %e %e %e\n", v1[0], v1[1], v1[2]);
        fprintf(f, "      vertex %e %e %e\n", v2[0], v2[1], v2[2]);
        fprintf(f, "      vertex %e %e %e\n", v3[0], v3[1], v3[2]);
        fprintf(f, "    endloop\n");
        fprintf(f, "  endfacet\n");
    }

    fprintf(f, "endsolid mesh\n");
    fclose(f);
    return 1;
}

/*----------------------------------------------------------------------------*/
/* Write binary STL                                                           */
/*----------------------------------------------------------------------------*/

static int writeBinarySTL(const char *filename, double (*vertices)[3],
                          int (*triangles)[3], int numTri)
{
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Cannot create file %s\n", filename);
        return 0;
    }

    /* 80-byte header */
    char header[80] = "Binary STL exported from meshb";
    fwrite(header, 1, 80, f);

    /* Triangle count */
    uint32_t triCount = (uint32_t)numTri;
    fwrite(&triCount, sizeof(uint32_t), 1, f);

    /* Write triangles */
    for (int i = 0; i < numTri; i++) {
        float normal[3];
        double *v1 = vertices[triangles[i][0]];
        double *v2 = vertices[triangles[i][1]];
        double *v3 = vertices[triangles[i][2]];

        computeNormal(v1, v2, v3, normal);

        /* Normal */
        fwrite(normal, sizeof(float), 3, f);

        /* Vertices */
        float fv[3];
        fv[0] = (float)v1[0]; fv[1] = (float)v1[1]; fv[2] = (float)v1[2];
        fwrite(fv, sizeof(float), 3, f);
        fv[0] = (float)v2[0]; fv[1] = (float)v2[1]; fv[2] = (float)v2[2];
        fwrite(fv, sizeof(float), 3, f);
        fv[0] = (float)v3[0]; fv[1] = (float)v3[1]; fv[2] = (float)v3[2];
        fwrite(fv, sizeof(float), 3, f);

        /* Attribute byte count */
        uint16_t attr = 0;
        fwrite(&attr, sizeof(uint16_t), 1, f);
    }

    fclose(f);
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
    int binary = 1;  /* Default to binary STL */
    int64_t inMsh;
    int ver, dim;

    printf("\n");
    printf("  *************************************** \n");
    printf("  MESH2STL Converter, version 1.0        \n");
    printf("  *************************************** \n");
    printf("\n");

    if (argc < 2) {
        printf("  Usage: mesh2stl input.mesh[b] [output.stl] [-ascii]\n");
        printf("\n");
        printf("  Converts Gamma Mesh Format to STL.\n");
        printf("  If output file is not specified, uses input name with .stl extension.\n");
        printf("\n");
        printf("  Options:\n");
        printf("    -ascii    Write ASCII STL instead of binary (larger file)\n");
        printf("\n");
        printf("  Note: Only triangular surface elements are exported.\n");
        printf("        Quads are split into two triangles.\n");
        printf("\n");
        return 0;
    }

    /* Parse arguments */
    inFile = argv[1];
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-ascii") == 0) {
            binary = 0;
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
        strcat(outBuf, ".stl");
        outFile = outBuf;
    }

    printf("  Input:  %s\n", inFile);
    printf("  Output: %s (%s)\n", outFile, binary ? "binary" : "ASCII");
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

    /* Read vertices */
    int numVer = (int)GmfStatKwd(inMsh, GmfVertices);
    if (numVer == 0) {
        fprintf(stderr, "Error: No vertices in mesh\n");
        GmfCloseMesh(inMsh);
        return 1;
    }

    double (*vertices)[3] = malloc((numVer + 1) * sizeof(double[3]));
    if (!vertices) {
        fprintf(stderr, "Error: Cannot allocate memory for vertices\n");
        GmfCloseMesh(inMsh);
        return 1;
    }

    GmfGotoKwd(inMsh, GmfVertices);
    for (int i = 1; i <= numVer; i++) {
        int ref;
        GmfGetLin(inMsh, GmfVertices, &vertices[i][0], &vertices[i][1],
                  &vertices[i][2], &ref);
    }

    printf("  Read %d vertices\n", numVer);

    /* Count triangles (including quads split into 2 triangles) */
    int numTri = (int)GmfStatKwd(inMsh, GmfTriangles);
    int numQuad = (int)GmfStatKwd(inMsh, GmfQuadrilaterals);
    int totalTri = numTri + 2 * numQuad;

    if (totalTri == 0) {
        fprintf(stderr, "Error: No triangles or quads in mesh\n");
        free(vertices);
        GmfCloseMesh(inMsh);
        return 1;
    }

    int (*triangles)[3] = malloc(totalTri * sizeof(int[3]));
    if (!triangles) {
        fprintf(stderr, "Error: Cannot allocate memory for triangles\n");
        free(vertices);
        GmfCloseMesh(inMsh);
        return 1;
    }

    int triIdx = 0;

    /* Read triangles */
    if (numTri > 0) {
        GmfGotoKwd(inMsh, GmfTriangles);
        for (int i = 0; i < numTri; i++) {
            int ref;
            GmfGetLin(inMsh, GmfTriangles,
                      &triangles[triIdx][0],
                      &triangles[triIdx][1],
                      &triangles[triIdx][2],
                      &ref);
            triIdx++;
        }
        printf("  Read %d triangles\n", numTri);
    }

    /* Read quads and split into triangles */
    if (numQuad > 0) {
        GmfGotoKwd(inMsh, GmfQuadrilaterals);
        for (int i = 0; i < numQuad; i++) {
            int v1, v2, v3, v4, ref;
            GmfGetLin(inMsh, GmfQuadrilaterals, &v1, &v2, &v3, &v4, &ref);

            /* First triangle: v1, v2, v3 */
            triangles[triIdx][0] = v1;
            triangles[triIdx][1] = v2;
            triangles[triIdx][2] = v3;
            triIdx++;

            /* Second triangle: v1, v3, v4 */
            triangles[triIdx][0] = v1;
            triangles[triIdx][1] = v3;
            triangles[triIdx][2] = v4;
            triIdx++;
        }
        printf("  Read %d quads (split into %d triangles)\n", numQuad, 2 * numQuad);
    }

    GmfCloseMesh(inMsh);

    printf("\n");
    printf("  Total triangles to export: %d\n", totalTri);
    printf("\n");

    /* Write STL */
    int success;
    if (binary) {
        success = writeBinarySTL(outFile, vertices, triangles, totalTri);
    } else {
        success = writeAsciiSTL(outFile, vertices, triangles, totalTri);
    }

    free(vertices);
    free(triangles);

    if (success) {
        printf("  Successfully wrote %s\n", outFile);
        printf("\n");
        return 0;
    } else {
        return 1;
    }
}
