/* ----------------------------- MNI Header -----------------------------------
   @NAME       : dicom_to_minc.c
   @DESCRIPTION: Code to convert a list of DICOM files to minc 
   format.
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : January 28, 1997 (Peter Neelin)
   @MODIFIED   : 
   * $Log: dicom_to_minc.c,v $
   * Revision 1.1  2005-02-17 16:38:10  bert
   * Initial checkin, revised DICOM to MINC converter
   *
   * Revision 1.1.1.1  2003/08/15 19:52:55  leili
   * Leili's dicom server for sonata
   *
   * Revision 1.18  2002/09/26 15:24:33  rhoge
   * Before was only skipping time sort for multi-slice N4 mosaics.  Turns out
   * this was also causing failure on single-slice scans.
   * Changed slices>1 to slices>0 so that now N4 dicom scans never get sorted
   * on their (apparently nonsensical) time value.  The if statement should
   * really be reworked, and should keep an eye on this.  Seems like EPI
   * time series sequences never sort properly on 'time'.
   *
   * Revision 1.17  2002/09/25 17:25:43  rhoge
   * changed public void's to public int's
   *
   * Revision 1.16  2002/05/08 19:32:40  rhoge
   * fixed handling of diffusion scans with separate series for each average
   *
   * Revision 1.15  2002/05/01 21:29:34  rhoge
   * removed MrProt from minc header - encountered files with large strings,
   * causing seg faults
   *
   * Revision 1.14  2002/04/30 12:36:35  rhoge
   * fixes to handle current (and hopefully final) diffusion sequence
   *
   * Revision 1.13  2002/04/26 03:27:03  rhoge
   * fixed MrProt problem - replaced fixed lenght char array with malloc
   *
   * Revision 1.12  2002/04/08 03:40:56  rhoge
   * fixed mosaic extraction for non-square scans and 3D scans.
   * added some new dicom elements
   *
   * Revision 1.11  2002/03/27 19:38:08  rhoge
   * small comment change
   *
   * Revision 1.10  2002/03/27 18:57:50  rhoge
   * added diffusion b value
   *
   * Revision 1.9  2002/03/23 13:17:53  rhoge
   * added support for Bourget network pushed dicom files, cleaned up
   * file check and read_numa4_dicom vr check/assignment
   *
   * Revision 1.8  2002/03/22 19:19:36  rhoge
   * Numerous fixes -
   * - handle Numaris 4 Dicom patient name
   * - option to cleanup input files
   * - command option
   * - list-only option
   * - debug mode
   * - user supplied name, idstr
   * - anonymization
   *
   * Revision 1.7  2002/03/21 13:31:56  rhoge
   * updated comments
   *
   * Revision 1.6  2002/03/19 22:10:16  rhoge
   * removed time sorting for N4DCM mosaics - time is random for mosaics
   *
   * Revision 1.5  2002/03/19 13:13:56  rhoge
   * initial working mosaic support - I think time is scrambled though.
   *
   * Revision 1.4  2001/12/31 18:27:21  rhoge
   * modifications for dicomreader processing of Numaris 4 dicom files - at
   * this point code compiles without warning, but does not deal with
   * mosaiced files.  Also will probably not work at this time for Numaris
   * 3 .ima files.  dicomserver may also not be functional...
   *
   * Revision 1.3  2000/12/14 21:37:11  rhoge
   * log message cleanup
   *
   * Revision 1.2  2000/12/14 21:36:22  rhoge
   * changes to restore measurement loop support that was broken by changes
   * to provide acquisition loop support
   *
   * Revision 1.1.1.1  2000/11/30 02:13:15  rhoge
   * imported sources to CVS repository on amoeba
   * -now support Siemens acquisition loop scans with and without correction
   *  on sending side
   *
   * Revision 6.1  1999/10/29 17:51:59  neelin
   * Fixed Log keyword
   *
   * Revision 6.0  1997/09/12 13:24:27  neelin
   * Release of minc version 0.6
   *
   * Revision 5.0  1997/08/21  13:25:26  neelin
   * Release of minc version 0.5
   *
   * Revision 4.0  1997/05/07  20:06:20  neelin
   * Release of minc version 0.4
   *
   * Revision 1.1  1997/03/04  20:56:47  neelin
   * Initial revision
   *
   @COPYRIGHT : Copyright 1997 Peter Neelin, McConnell Brain Imaging
   Centre, Montreal Neurological Institute, McGill University.
   Permission to use, copy, modify, and distribute this software and
   its documentation for any purpose and without fee is hereby
   granted, provided that the above copyright notice appear in all
   copies.  The author and McGill University make no representations
   about the suitability of this software for any purpose.  It is
   provided "as is" without express or implied warranty.
   ---------------------------------------------------------------------------- */

#include "dcm2mnc.h"

const char *World_Names[WORLD_NDIMS] = { "X", "Y", "Z" };

/* Private structure definitions. */

typedef enum {
    MOSAIC_SEQ_UNKNOWN,
    MOSAIC_SEQ_INTERLEAVED,
    MOSAIC_SEQ_ASCENDING,
    MOSAIC_SEQ_DESCENDING
} mosaic_seq_t;

/* multi-image (mosaic) info */
typedef struct {
    int packed;
    mosaic_seq_t mosaic_seq;
    int size[2];
    int big[2];
    int grid[2];
    int pixel_size;
    Acr_Element big_image;
    Acr_Element small_image;
    int sub_images;
    int slice_count;
    double normal[3];
    double step[3];
    double position[3];
} Multi_Image;

/* Structure for sorting dimensions */
typedef struct {
   int identifier;
   int original_index;
   double value;
} Sort_Element;

/* Private function definitions */
static int multi_image_init(Acr_Group, Multi_Image *, int);
static void multi_image_cleanup(Acr_Group, Multi_Image *);
static int multi_image_modify_group_list(Acr_Group, Multi_Image *, int, int);

static void free_info(General_Info *general_info, File_Info *file_info, 
                      int num_files);
static int dimension_sort_function(const void *v1, const void *v2);
static void sort_dimensions(General_Info *general_info);
static int prot_find_string(Acr_Element Protocol, const char *name,
                            char *value);
static char *dump_protocol_text(Acr_Element Protocol);

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : dicom_to_minc
   @INPUT      : num_files - number of image files
   file_list - list of file names
   minc_file - name of minc file to create, or NULL to make one up.
   clobber - if TRUE, then open the output with NC_CLOBBER
   file_prefix - string providing any directory or prefix 
   for internally generated filename (if it is a directory,
   then it must contain the last "/")
   @OUTPUT     : output_file_name - returns a pointer to an internal area
   containing the file name of the created file if minc_file
   is NULL, or simply a pointer to minc_file. If NULL, then
   nothing is returned.
   @RETURNS    : EXIT_SUCCESS if no error, EXIT_FAILURE on error.
   @DESCRIPTION: Routine to convert a list of Siemens dicom files to minc 
   format.
   @METHOD     : 
   @GLOBALS    :
   @CALLS      : 
   @CREATED    : November 25, 1993 (Peter Neelin)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */
int
dicom_to_minc(int num_files, 
              const char *file_list[], 
              const char *minc_file,
              int clobber,
              const char *file_prefix, 
              char **output_file_name)
{
    Acr_Group group_list;       /* List of ACR/NEMA groups & elements */
    File_Info *file_info;       /* Array of per-file information */
    General_Info general_info;  /* General (common) DICOM file information */
    int max_group;              /* Maximum group number to read */
    Image_Data image;           /* Actual image data */
    int icvid;                  /* MINC Image Conversion Variable */
    int ifile;                  /* File index */
    Mri_Index imri;             /* MRI axis index */
    char *out_file_name;
    int isep;
    Loop_Type loop_type = NONE;
    int subimage;               /* Loop counter for MOSAIC images per file */
    int iimage;                 /* Loop counter for all files/images */
    int num_images_allocated;
    Multi_Image multi_image;

    /* Allocate space for the file information */
    file_info = malloc(num_files * sizeof(*file_info));
    CHKMEM(file_info);

    num_images_allocated = num_files;

    /* Last group needed for first pass
     */
    max_group = ACR_IMAGE_GID - 1;

    /* Add all control characters as numeric array separators to handle 
     * odd behaviour with Siemens dicom files
     */
    for (isep = 0; isep < 31; isep++) {
        acr_element_numeric_array_separator(isep);
    }

    /* Initialize some values for general info */
    general_info.initialized = FALSE;
    general_info.group_list = NULL;
    for (imri = 0; imri < MRI_NDIMS; imri++) {
        general_info.indices[imri] = NULL;
        general_info.coordinates[imri] = NULL;
    }

    /* Loop through file list getting information
     * (note that we have to duplicate the handling
     * of multiple images per file in this loop
     * to accumulate dimension sizes correctly)

     * need separate counter for images, since some files may
     * contain more than one image!
     */
    iimage = 0;

    for (ifile = 0; ifile < num_files; ifile++) {
        int n_slices_in_file;

        if (!G.Debug) {
            progress(ifile, num_files, "-Parsing series info");
        }

        /* Read the file
         */
        if (G.file_type == N4DCM) {
            group_list = read_numa4_dicom(file_list[ifile], max_group);
        } 
        else if (G.file_type == IMA) {
            group_list = siemens_to_dicom(file_list[ifile], max_group);
        }

        if (group_list == NULL) {
            fprintf(stderr, "Error parsing file '%s' on 1st pass.\n",
                    file_list[ifile]);
            exit(-1);
        }

        n_slices_in_file = acr_find_int(group_list, EXT_Slices_in_file, 1);

        /* initialize big and small images, if mosaic
         */
        if (n_slices_in_file > 1) {

            multi_image_init(group_list, &multi_image, FALSE);

            num_images_allocated += n_slices_in_file - 1;

            file_info = realloc(file_info, 
                                num_images_allocated * sizeof(*file_info));
            CHKMEM(file_info);
        }

        /* loop over subimages in mosaic
         */
        for (subimage = 0; subimage < n_slices_in_file; subimage++) {

            /* Modify the group list for this image if mosaic
             */
            if (n_slices_in_file > 1) {
                multi_image_modify_group_list(group_list, &multi_image,
                                              subimage, FALSE);
            }

            /* Get file-specific information
             */
            get_file_info(group_list, &file_info[iimage], &general_info);

            /* increment iimage here
             */
            iimage++;
        }

        /* Delete the group list
         */
        acr_delete_group_list(group_list);

        /* cleanup multi_image struct if used
         */
        if (general_info.num_slices_in_file > 1) {
            multi_image_cleanup(group_list, &multi_image);
        }
    }

    /* Sort the dimensions */
    sort_dimensions(&general_info);

    /* Create the output file
     */
    if (general_info.initialized) {
        icvid = create_minc_file(minc_file,
                                 clobber,
                                 &general_info,
                                 file_prefix,
                                 &out_file_name,
                                 loop_type);
    }
    if (output_file_name != NULL) {
        *output_file_name = out_file_name;
    }

    /* Check that we found the general info and that the minc file was
     * created okay
     */
    if ((!general_info.initialized) || (icvid == MI_ERROR)) {
        if (general_info.initialized) {
            fprintf(stderr, "Error creating MINC file %s.\n", out_file_name);
        }
        free_info(&general_info, file_info, num_files);
        free(file_info);
        return EXIT_FAILURE;
    }

    if (G.Debug) {
        printf("Writing %d images to MINC file\n", num_files);
    }	  

    /* Last group needed for second pass
     * we now have to read up to and including the image, 
     * since image pointers are needed in multi_image_init
     */
    max_group = ACR_IMAGE_GID;

    /* Loop through the files again and put images into the minc file
     */
    iimage = 0;
    for (ifile = 0; ifile < num_files; ifile++) {

        if (!G.Debug) {
            progress(ifile, num_files, "-Creating minc file");
        }

        /* Check that we have a valid file 
         */
        if (!file_info[ifile].valid) {
            printf("WARNING: file %s was marked invalid\n", 
                   file_list[ifile]);
            continue;
        }
     
        /* Read the file 
         */
        if (G.file_type == N4DCM) {
            group_list = read_numa4_dicom(file_list[ifile], max_group);
        }
        else if (G.file_type == IMA) {
            group_list = siemens_to_dicom(file_list[ifile], max_group);
        }
       
        if (group_list == NULL) {
            fprintf(stderr, "Error parsing file '%s' during 2nd pass.\n",
                    file_list[ifile]);
            exit(-1);
        }

        /* initialize big and small images, if mosaic
         */
        if (general_info.num_slices_in_file > 1) {
            multi_image_init(group_list, &multi_image, TRUE);
        }

        /* loop over subimages in mosaic
         */
        for (subimage = 0; subimage < general_info.num_slices_in_file; 
             subimage++) {

            /* Modify the group list for this image if mosaic
             */
            if (general_info.num_slices_in_file > 1) {
                multi_image_modify_group_list(group_list, &multi_image, 
                                              subimage, TRUE);
            }
       
            /* Get image
             */
            get_siemens_dicom_image(group_list, &image);
       
            /* Save the image and any other information
             */
            save_minc_image(icvid, &general_info, &file_info[iimage], &image);

            /* increment image counter
             */
            iimage++;
        }
     
        /* Delete the group list
         */
        acr_delete_group_list(group_list);

        /* cleanup multi_image struct if used
         */
        if (general_info.num_slices_in_file > 1) {
            multi_image_cleanup(group_list, &multi_image);
        }
     
        /* Free the image data */
        if ((image.data != NULL) && (image.free)) {
            free(image.data);
        }
    }

    /* Close the output file */
    close_minc_file(icvid);

    /* Free the general_info and file_info stuff */
    free_info(&general_info, file_info, num_files);
    free(file_info);

    return EXIT_SUCCESS;
}

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : read_numa4_dicom
   @INPUT      : filename - name of siemens Numaris 4 `dicom' file to read
   max_group - maximum group number to read
   @OUTPUT     : (none)
   @RETURNS    : group list read in from file
   @DESCRIPTION: Routine to read in a group list from a file.
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : December 18, 2001 (Rick Hoge)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */

Acr_Group
read_std_dicom(const char *filename, int max_group)
{
    FILE *fp;
    Acr_File *afp;
    Acr_Group group_list;
    int status;

    /* Open the file
     */
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        return NULL;
    }

    /* Connect to input stream
     */
    afp = acr_file_initialize(fp, 0, acr_stdio_read);
    if (afp == NULL) {
        return NULL;
    }

    if (acr_test_dicom_file(afp) != ACR_OK) {
        return NULL;
    }

    // Read in group list
    status = acr_input_group_list(afp, &group_list, max_group);
    if (status != ACR_END_OF_INPUT && status != ACR_OK) {
        return NULL;
    }

    // Close the file
    acr_file_free(afp);
    fclose(fp);

    return (group_list);
}

Acr_Group
read_numa4_dicom(const char *filename, int max_group)
{
    Acr_Group group_list;

    /* needed for group repair - some essential info
     * only available in ascii dump of MrProt structure
     */
    Acr_Element protocol; 
    Acr_Element element;
    int mosaic_rows, mosaic_cols;
    short subimage_size[4];
    int subimage_rows, subimage_cols;
    int num_slices, num_partitions;
    char *field_ptr;
    int num_encodings;
    int enc_ix;
    string_t str_buf;
    char *str_ptr;

    group_list = read_std_dicom(filename, max_group);
    if (group_list == NULL) {
        return NULL;
    }

    /* now fix the group list to provide essential info
     * via standard dicom elements
     * (this lets the rest of the code be more reusable)

     * Note that these parameters are mostly dimension lengths,
     * and are not usually supplied in standard DICOM implementations.
     * We could do without them, except that the use of mosaics for
     * multi-slice data makes at least the number of slices necessary.
     * For such elements, we will spoof our own `private' entries
     * using Numaris 3.5 coordinates.  These should not be used elsewhere
     * in the code unless there's no other way!  Basically things
     * should be done as follows:

     * 1) use actual element in public dicom group, if possible
     * 2) if not, then get info from MrProt and insert as 
     *    correct public dicom element
     * 3) if no public element exists, use an SPI element (careful!)
     * 4) if no SPI element exists, use and EXT element (careful!)
     */

    /* read in Protocol group */

    protocol = acr_find_group_element(group_list, SPI_Protocol);
    if (protocol == NULL) {
        if (G.Debug > 1) {
            printf("No Siemens protocol structure found...\n");
        }
        return group_list;
    }

    if (G.Debug > 1) {
        printf("Incorporating Siemens protocol structure...\n");
    }

    /* Add number of dynamic scans:
     */
    prot_find_string(protocol, "lRepetitions", str_buf);
    acr_insert_numeric(&group_list, ACR_Acquisitions_in_series,
                       atoi(str_buf) + 1);

    /* add number of echoes:
     */
    prot_find_string(protocol, "lContrasts", str_buf);
    acr_insert_numeric(&group_list, SPI_Number_of_echoes, atoi(str_buf));

    /* add receiving coil (for some reason this isn't in generic groups)
     */
    prot_find_string(protocol,
                     "sCOIL_SELECT_MEAS.asList[0].sCoilElementID.tCoilID",
                     str_buf);
    acr_insert_string(&group_list, ACR_Receiving_coil, str_buf);

    /* add MrProt dump
     */
    str_ptr = dump_protocol_text(protocol);
    acr_insert_string(&group_list, EXT_MrProt_dump, str_ptr);
    free(str_ptr);

    /* add number of slices: (called `Partitions' for 3D) */
    prot_find_string(protocol, "sSliceArray.lSize", str_buf);
    num_slices = atoi(str_buf);
        
    prot_find_string(protocol, "sKSpace.lPartitions", str_buf);
    num_partitions = atoi(str_buf);

    /* This is a hack based upon the observation that for at least some
     * conversions, this value seems to give the true number of slices
     * rather than the sKSpace.lPartitions value (bert)
     */
    prot_find_string(protocol, "sKSpace.lImagesPerSlab", str_buf);
    if (str_buf[0] != '\0') {
        int num_images_per_slab = atoi(str_buf);
        if (num_images_per_slab > num_partitions) {
            num_partitions = num_images_per_slab;
        }
    }
    
    /* NOTE:  for some reason, lPartitions > 1 even for 2D scans
     * (e.g. EPI, scouts)
     */
    if (!strncmp(acr_find_string(group_list, ACR_MR_acquisition_type,""),
                 "3D", 2)) {
        /* Use partitions if 3D.
         * (note that this gets more complicated if the 3D scan
         *  is mosaiced - see below)
         */
        acr_insert_numeric(&group_list, SPI_Number_of_slices_nominal, 1);
        acr_insert_numeric(&group_list, 
                           SPI_Number_of_3D_raw_partitions_nominal, 
                           num_partitions);
    } 
    else {
        /* use slices for 2D
         */
        acr_insert_numeric(&group_list, SPI_Number_of_slices_nominal, 
                           num_slices);
        acr_insert_numeric(&group_list, 
                           SPI_Number_of_3D_raw_partitions_nominal, 1);
    }

    /* now figure out mosaic rows and columns, and put in EXT shadow group
     * check for interpolation - not supported for mosaics yet
     */
    prot_find_string(protocol, "sKSpace.uc2DInterpolation", str_buf);
    if (strcmp(str_buf, "0")) { /* Not zero - implies interpolation. */
        /* if interpolated image, assume no mosaic */
        acr_insert_numeric(&group_list, EXT_Mosaic_rows, 1);
        acr_insert_numeric(&group_list, EXT_Mosaic_columns, 1);
        acr_insert_numeric(&group_list, EXT_Slices_in_file, 1);
    } 
    else {
        /* compute mosaic rows and columns
         * here is a hack to handle non-square mosaiced images
         * WARNING:  as far as I can tell, the phase-encoding dir
         * (row/col) is reversed for mosaic EPI scans (don't know
         * if this is a mosaic thing, an EPI thing, or whatever)
         * get the array of sizes:
         *    freq row/freq col/phase row/phase col
         */
         element = acr_find_group_element(group_list, ACR_Acquisition_matrix);
         acr_get_element_short_array(element, 4, subimage_size);

         
         /* get subimage dimensions, assuming the OPPOSITE of the
          * reported phase-encode direction!!
          */
         if (!strncmp(acr_find_string(group_list, 
                                      ACR_Phase_encoding_direction, ""),
                      "COL", 3)) {

             subimage_rows = subimage_size[3];
             subimage_cols = subimage_size[0];
         }
         else if (!strncmp(acr_find_string(group_list,
                                           ACR_Phase_encoding_direction,""),
                           "ROW", 3)) {

             subimage_rows = subimage_size[2];
             subimage_cols = subimage_size[1];
         }

         mosaic_rows = acr_find_int(group_list, ACR_Rows, 1) / subimage_rows;
         mosaic_cols = acr_find_int(group_list, ACR_Columns, 1) / subimage_cols;

         acr_insert_numeric(&group_list, EXT_Mosaic_rows, mosaic_rows);
         acr_insert_numeric(&group_list, EXT_Mosaic_columns, mosaic_cols);

         if (mosaic_rows * mosaic_cols > 1) {

             /* if 3D mosaiced scan, write number of partitions to number of 
              * slices in dicom group  THIS LOOKS REDUNDANT!!!
              */
             if (!strncmp(acr_find_string(group_list, ACR_MR_acquisition_type,
                                          ""),"3D",2)) {
                 acr_insert_numeric(&group_list, SPI_Number_of_slices_nominal,
                                    num_partitions);
             }
             
             /* assume any mosaiced file contains all slices
              * (we now support mosaics for 2D and 3D acquisitions,
              *  so we may need to use partitions instead of slices)
              */
       
             if (!strncmp(acr_find_string(group_list,ACR_MR_acquisition_type,""),
                          "2D",2)) {
                 acr_insert_numeric(&group_list, EXT_Slices_in_file, num_slices);
                 acr_insert_numeric(&group_list, 
                                    SPI_Number_of_slices_nominal,num_slices);
             } 
             else if (!strncmp(acr_find_string(group_list, ACR_MR_acquisition_type,
                                               ""),"3D",2)) {
                 acr_insert_numeric(&group_list, EXT_Slices_in_file, 
                                    num_partitions);
                 acr_insert_numeric(&group_list, SPI_Number_of_slices_nominal, 
                                    num_partitions);
                 /* also have to provide slice spacing - in case of 3D it's same
                  * as slice thickness (and not provided in dicom header!)
                  */
                 acr_insert_numeric(&group_list, ACR_Spacing_between_slices,
                                    acr_find_double(group_list, 
                                                    ACR_Slice_thickness, 1.0));
             }
         } 
         else {
             acr_insert_numeric(&group_list, EXT_Slices_in_file, 1);
         }

         /* correct the rows and columns values -
          * these will reflect those of the subimages in the mosaics
          * NOT the total image dimensions
          */
         acr_insert_short(&group_list, EXT_Sub_image_columns,subimage_cols);
         acr_insert_short(&group_list, EXT_Sub_image_rows,subimage_rows);

         /* should also correct the image position here? */
    }

    /* correct dynamic scan info if diffusion scan:
     *
     * assumptions:
     *
     *  - diffusion protocol indicated by sDiffusion.ucMode = 0x4
     *  - there are 7 shots for DTI (b=0 + 6 encodings)
     *  - b=0 scan has sequence name "ep_b0"
     *  - encoded scans have seq names "ep_b700#1, ep_b700#2, ..." etc.
     *
     * actions:
     * 
     *  - change number of dynamic scans to 7
     *  - modify dynamic scan index to encoding index
     */
     prot_find_string(protocol, "sDiffusion.ucMode", str_buf);
     if (!strcmp(str_buf, "0x4")) {

         /* try to get b value */
         prot_find_string(protocol, "sDiffusion.alBValue[1]", str_buf);
         acr_insert_numeric(&group_list, EXT_Diffusion_b_value,
                            atoi(str_buf));

         /* if all averages in one series: */
         prot_find_string(protocol, "ucOneSeriesForAllMeas", str_buf);
         if (!strcmp(str_buf, "0x1")) {

             num_encodings = 7; /* for now assume 7 shots in diffusion scan */

             /* number of 'time points' */
             acr_insert_numeric(&group_list, ACR_Acquisitions_in_series, 
                                num_encodings*
                                acr_find_double(group_list,
                                                ACR_Nr_of_averages, 1));

             /* time index of current scan: */
                
             /* In the current scheme, the unencoded scan has a
              * sequence name like "ep_b0" while the subsequent six
              * diffusion encodings have names like "ep_b700#1" we
              * could use this to come up with indices for an encoding
              * dimension
              */
             field_ptr = strstr(acr_find_string(group_list,
                                                ACR_Sequence_name,""),"#");
             if (field_ptr == NULL) {
                 enc_ix = 0;
             } 
             else {
                 enc_ix = atoi(field_ptr + sizeof(char));
             }
                
             /* however with the current sequence, we get usable
              * time indices from floor(global_image_num/num_slices)
              */
              acr_insert_numeric(&group_list, ACR_Acquisition, 
                                 (acr_find_int(group_list, ACR_Image, 1)-1) / 
                                 num_slices);

         } 
         else { /* averages in different series - no special handling needed? */

             num_encodings = 7; /* for now assume 7 shots in diffusion scan */

             /* number of 'time points' */
             acr_insert_numeric(&group_list, ACR_Acquisitions_in_series,
                                num_encodings);
                
             /* For multi-series scans, we DO USE THIS BECAUSE global
              * image number may be broken!!
              */
             field_ptr = strstr(acr_find_string(group_list,
                                                ACR_Sequence_name, ""),
                                "#");
             if (field_ptr == NULL) {
                 enc_ix = 0;
             } 
             else {
                 enc_ix = atoi(field_ptr + sizeof(char));
             }
             acr_insert_numeric(&group_list, ACR_Acquisition, enc_ix);
         }
     } // end of diffusion scan handling

     return (group_list);
}

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : free_info
   @INPUT      : general_info
   file_info
   num_files
   @OUTPUT     : (none)
   @RETURNS    : (nothing)
   @DESCRIPTION: Routine to free contents of general and file info structures.
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : November 26, 1993 (Peter Neelin)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */

static void
free_info(General_Info *general_info, File_Info *file_info, int num_files)
{
    Mri_Index imri;

    /* Free the general info pointers */
    for (imri=0; imri < MRI_NDIMS; imri++) {
        if (general_info->indices[imri] != NULL) {
            free(general_info->indices[imri]);
        }
        if (general_info->coordinates[imri] != NULL) {
            free(general_info->coordinates[imri]);
        }
    }

    /* Free the group list */
    if (general_info->group_list != NULL) {
        acr_delete_group_list(general_info->group_list);
    }

    return;

}

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : search_list
   @INPUT      : value
   list_ptr
   list_length
   start_index - point from which search should start
   @OUTPUT     : (none)
   @RETURNS    : Index in list where value is found, or -1 is value not found.
   @DESCRIPTION: Routine to search a list for a value, returning the index
   into the list. If the value is not found, then -1 is returned.
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : February 28, 1997 (Peter Neelin)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */
int
search_list(int value, const int *list_ptr, int list_length, int start_index)
{
    int index;

    /* If nothing on list, just return. */
    if (list_length <= 0) {
        return (-1);
    }

    /* If starting point is invalid, start at zero.
     */
    if ((start_index >= list_length) || (start_index < 0)) {
        start_index = 0;
    }

    /* Loop over indices, wrapping at the end of the list */
    index = start_index;
    do {
        if (list_ptr[index] == value) {
            return index;       /* Found it. */
        }
        index++;
        if (index >= list_length) {
            index = 0;
        }
    } while (index != start_index);

    return -1;                  /* Search failed. */
}

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : sort_dimensions
   @INPUT      : general_info
   @OUTPUT     : general_info
   @RETURNS    : (nothing)
   @DESCRIPTION: Routine to sort the MRI dimensions according to their 
   coordinates. It also fills in the step and start values for 
   the SLICE dimension.
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : February 28, 1997 (Peter Neelin)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */
static void
sort_dimensions(General_Info *general_info)
{
    Mri_Index imri;
    Sort_Element *sort_array;
    int nvalues, ival, jval;
    int reverse_array;

    /* Sort the dimensions, if needed */
    for (imri = 0; imri < MRI_NDIMS; imri++) {
        if (general_info->cur_size[imri] > 1 &&
            !((G.file_type == N4DCM) && // don't sort on time for N4 mosaics!
              (imri == TIME) && 
              (general_info->num_slices_in_file > 0))) { // also fails on 1 slice

            /* Set up the array for sorting */
            nvalues = general_info->cur_size[imri];
            sort_array = malloc(nvalues * sizeof(*sort_array));
            CHKMEM(sort_array);
            for (ival=0; ival < nvalues; ival++) {
                sort_array[ival].identifier = general_info->indices[imri][ival];
                sort_array[ival].original_index = ival;
                sort_array[ival].value = general_info->coordinates[imri][ival];
            }

            /* Sort the array */
            qsort((void *) sort_array, (size_t) nvalues, sizeof(*sort_array), 
                  dimension_sort_function);

            /* Figure out if we should reverse the array to keep something 
               similar to the original ordering */
            reverse_array = (sort_array[0].original_index > 
                             sort_array[nvalues-1].original_index);

            /* Copy the information back into the appropriate arrays */
            for (ival=0; ival < nvalues; ival++) {
                jval = (reverse_array ? nvalues - ival - 1 : ival);
                general_info->indices[imri][ival] = sort_array[jval].identifier;
                general_info->coordinates[imri][ival] = sort_array[jval].value;
            }

            /* Free the array */
            free(sort_array);

            /* Update slice step and start */
            if (imri == SLICE) {
                if (general_info->coordinates[imri][0] != 
                    general_info->coordinates[imri][nvalues-1]) {
                    double dbl_tmp1;
                    double dbl_tmp2;

                    dbl_tmp1 = general_info->step[general_info->slice_world];
                    dbl_tmp2 = 
                        (general_info->coordinates[imri][nvalues-1] -
                         general_info->coordinates[imri][0]) / 
                        ((double) general_info->cur_size[imri] - 1.0);

                    if (dbl_tmp1 != dbl_tmp2) {
                        printf("WARNING: calculated slice width (%f) disagrees with file's slice width (%f)\n", dbl_tmp2, dbl_tmp1);
                        if (dbl_tmp1 == 1.0) {
                            general_info->step[general_info->slice_world] = dbl_tmp2;
                        }
                    }

                }
                general_info->start[general_info->slice_world] =
                    general_info->coordinates[imri][0];
                if (G.Debug > 1) {
                    printf("Set slice %d step to %f, start to %f\n",
                           general_info->slice_world,
                           general_info->step[general_info->slice_world],
                           general_info->start[general_info->slice_world]);
                }
            }

        }       /* If size > 1 */
    }          /* Loop over dimensions */

}

/* ----------------------------- MNI Header -----------------------------------
   @NAME       : dimension_sort_function
   @INPUT      : v1, v2 - values to compare
   @OUTPUT     : (none)
   @RETURNS    : -1, 0 or 1 if v1 < v2, v1 == v2 or v1 > v2
   @DESCRIPTION: Function to compare to array elements for sorting. Elements are
   compared first on value, then on their original array index
   (this tries to preserve the original sequence).
   @METHOD     : 
   @GLOBALS    : 
   @CALLS      : 
   @CREATED    : February 28, 1997 (Peter Neelin)
   @MODIFIED   : 
   ---------------------------------------------------------------------------- */
static int
dimension_sort_function(const void *v1, const void *v2)
{
    Sort_Element *value1, *value2;

    value1 = (Sort_Element *) v1;
    value2 = (Sort_Element *) v2;

    if (value1->value < value2->value)
        return -1;
    else if (value1->value > value2->value)
        return 1;
    else if (value1->original_index < value2->original_index)
        return -1;
    else if (value1->original_index > value2->original_index)
        return 1;
    else
        return 0;
}

int
prot_find_string(Acr_Element elem_ptr, const char *name_str, char *value)
{
    static const char prot_head[] = "### ASCCONV BEGIN ###";
    long cur_offset;
    long max_offset;
    char *field_ptr;
    int  ix1, ix2;

    max_offset = elem_ptr->data_length - sizeof(prot_head);

    // Scan through the element containing the protocol, to find the 
    // ASCII dump of the MrProt structure.
    //
    for (cur_offset = 0; cur_offset < max_offset; cur_offset++) {
        if (!memcmp(elem_ptr->data_pointer + cur_offset,
                    prot_head, sizeof(prot_head) - 1)) {
            break;
        }
    }

    /* bail if we didn't find the protocol
     */
    if (cur_offset == max_offset) {
        return (0);
    }

    field_ptr = strstr(elem_ptr->data_pointer + cur_offset, name_str);
    if (field_ptr != NULL) {
        sscanf(field_ptr, "%*s %*s %s", value);
        ix1 = 0;
        for (ix2 = 0; value[ix2] != '\0'; ix2++) {
            if (value[ix2] != '"') {
                value[ix1++] = value[ix2];
            }
        }
    } 
    else {
        strcpy(value, "0");
    }
    return (1);
}

static char *
dump_protocol_text(Acr_Element elem_ptr)
{
    const char prot_head[] = "### ASCCONV BEGIN ###";
    const char prot_tail[] = "### ASCCONV END ###";
    char *output = malloc(elem_ptr->data_length);
    int prot_found = FALSE;
    long cur_offset;
    long max_offset;

    CHKMEM(output);

    // scan throught the group containing the protocol, to find the 
    // ascii dump of the MrProt structure
    max_offset = elem_ptr->data_length - sizeof (prot_head);

    for (cur_offset = 0; cur_offset < max_offset; cur_offset++) {
        if (!memcmp(elem_ptr->data_pointer + cur_offset,
                    prot_head, sizeof(prot_head) - 1)) {
            prot_found = TRUE;
            break;
        }
    }

    if (prot_found) {
        int ix1 = 0;
        char *tmp_ptr = elem_ptr->data_pointer + cur_offset;

        for ( ; cur_offset < max_offset; cur_offset++) {
            if (!memcmp(tmp_ptr, prot_tail, sizeof(prot_tail) - 1)) {
                break;      
            }
            if (*tmp_ptr != '"') {
                output[ix1++] = *tmp_ptr;
            }
            tmp_ptr++;
        }
        output[ix1] = '\0';
    }
    return (output);
}

static int 
multi_image_init(Acr_Group group_list, Multi_Image *multi_image, 
                 int load_image)
{
    int group_id, element_id;
    int grid_size;
    long new_image_size;
    void *data;
    Acr_Element element;
    string_t string;
    int idim;
    double pixel_spacing[2];
    double separation;
    double RowColVec[6];
    double dircos[VOL_NDIMS][WORLD_NDIMS];
    char *str_tmp;

    str_tmp = acr_find_string(group_list, SPI_Order_of_slices, "");
    if (!strncmp(str_tmp, "INTERLEAVED", 11)) {
        multi_image->mosaic_seq = MOSAIC_SEQ_INTERLEAVED;
    }
    else if (!strncmp(str_tmp, "ASCENDING", 9)) {
        multi_image->mosaic_seq = MOSAIC_SEQ_ASCENDING;
    }
    else if (!strncmp(str_tmp, "DESCENDING", 10)) {
        multi_image->mosaic_seq = MOSAIC_SEQ_DESCENDING;
    }
    else {
        multi_image->mosaic_seq = MOSAIC_SEQ_UNKNOWN;
    }

    // Get some basic image information
    // (big[0/1] is number of columns/rows in whole mosaic)
    multi_image->big[0] = acr_find_int(group_list, ACR_Columns, 1);
    multi_image->big[1] = acr_find_int(group_list, ACR_Rows, 1);
    multi_image->pixel_size = 
        (acr_find_int(group_list, ACR_Bits_allocated, 16) - 1) / 8 + 1;

    // Get the image size
    // (size[0/1] is number of columns/rows in a single slice)
    multi_image->size[0] = acr_find_short(group_list, EXT_Sub_image_columns, 1);
    multi_image->size[1] = acr_find_short(group_list, EXT_Sub_image_rows, 1);

    // Get the grid shape, checking that it is not too big if specified
    multi_image->grid[0] = multi_image->big[0] / multi_image->size[0];
    multi_image->grid[1] = multi_image->big[1] / multi_image->size[1];
    if ((multi_image->grid[0] < 1) || (multi_image->grid[0] < 1)) {
        fprintf(stderr, "Grid too small: %d x %d\n",
                multi_image->grid[0], multi_image->grid[1]);
        exit(EXIT_FAILURE);
    }

    // Check whether we need to do anything (1x1 grid may be the whole image)
    grid_size = multi_image->grid[0] * multi_image->grid[1];
    if ((grid_size == 1) &&
        (multi_image->size[0] == multi_image->big[0]) &&
        (multi_image->size[1] == multi_image->big[1])) {
        /* had to remove this as now ANY images acquired with 
           the mosaic sequence need special treatment */
        multi_image->packed = FALSE;
        return 1;
    }

    // Update the number of image rows and columns
    acr_insert_short(&group_list, ACR_Rows, multi_image->size[1]);
    acr_insert_short(&group_list, ACR_Columns, multi_image->size[0]);

    // Get image image index info (number of slices in file)
    multi_image->slice_count = acr_find_int(group_list, EXT_Slices_in_file, 1);

    // sub_images is now just the number of mosaic elements, even if
    // they don't all contain slices 
    multi_image->sub_images = multi_image->grid[0] * multi_image->grid[1];


    // get the pixel size
    element = acr_find_group_element(group_list, ACR_Pixel_size);
    if ((element != NULL) &&
        (acr_get_element_numeric_array(element, 2, pixel_spacing) == 2)) {
     
        // adjust pixel size for old Numaris 3.5 data
        if (G.file_type == IMA || G.file_type == N3DCM) {
       
            pixel_spacing[0] *= 
                (double) multi_image->big[0] / (double) multi_image->size[0];
            pixel_spacing[1] *= 
                (double) multi_image->big[1] / (double) multi_image->size[1];
            sprintf(string, "%.15g\\%.15g", 
                    pixel_spacing[0], pixel_spacing[1]);
            acr_insert_string(&group_list, ACR_Pixel_size, string);
        }
    }

    // Get step between slices
    separation = acr_find_double(group_list, ACR_Spacing_between_slices, 1.0);

    // get image normal vector
    // (need to compute based on dicom field, which gives
    //  unit vectors for row and column direction)
    element = acr_find_group_element(group_list, ACR_Image_orientation_patient);
    acr_get_element_numeric_array(element, 6, RowColVec);
   
    memcpy(dircos[VCOLUMN], RowColVec, sizeof(RowColVec[0]) * 3);
    memcpy(dircos[VROW], &RowColVec[3], sizeof(RowColVec[0]) * 3);
   
    // compute slice normal as cross product of row/column unit vectors
    // (should check for unit length?)
    multi_image->normal[0] = 
        dircos[VCOLUMN][1] * dircos[VROW][2] -
        dircos[VCOLUMN][2] * dircos[VROW][1];
   
    multi_image->normal[1] = 
        dircos[VCOLUMN][2] * dircos[VROW][0] -
        dircos[VCOLUMN][0] * dircos[VROW][2];
   
    multi_image->normal[2] = 
        dircos[VCOLUMN][0] * dircos[VROW][1] -
        dircos[VCOLUMN][1] * dircos[VROW][0];

    // compute slice-to-slice step vector
    for (idim=0; idim < 3; idim++) {
        multi_image->step[idim] = separation * multi_image->normal[idim];
    }

    // Get position and correct to first slice
    element = acr_find_group_element(group_list, ACR_Image_position_patient);
    acr_get_element_numeric_array(element, WORLD_NDIMS,multi_image->position);

    if (G.file_type == IMA || G.file_type == N3DCM) {
        // Numaris 3.5 style correction:
        // (position in file is for last slice, we want first)
        for (idim=0; idim < 3; idim++) {
            multi_image->position[idim] -= 
                (double) (multi_image->sub_images-1) * multi_image->step[idim];
        } 
    } 
    else {
        // Numaris 4 mosaic correction:
        // - position given is edge of huge slice constructed as if 
        //   real slice was at center of mosaic
        // - multi_image->big[0,1] are number of columns and rows of mosaic
        // - multi_image->size[0,1] are number of columns and rows of sub-image

        for (idim=0; idim < 3; idim++) {

            // correct offset from mosaic Center
            multi_image->position[idim] += (double)
                ((dircos[VCOLUMN][idim]*multi_image->big[0]*pixel_spacing[0]/2.0) +
                 (dircos[VROW][idim] * multi_image->big[1] * pixel_spacing[1]/2));

            // move from center to corner of slice
            multi_image->position[idim] -= 
                dircos[VCOLUMN][idim] * multi_image->size[0] * pixel_spacing[0]/2.0 +
                dircos[VROW][idim] * multi_image->size[1] * pixel_spacing[1]/2.0;
	 
        } 

    }

    if (load_image) {

        // Steal the image element from the group list
        multi_image->big_image = acr_find_group_element(group_list,
                                                        ACR_Pixel_data);
        if (multi_image->big_image == NULL) {
            fprintf(stderr, "Couldn't find an image\n");
            exit(EXIT_FAILURE);
        }
        group_id = acr_get_element_group(multi_image->big_image);
        element_id = acr_get_element_element(multi_image->big_image);
        acr_group_steal_element(acr_find_group(group_list, group_id),
                                multi_image->big_image);
        
        // Add a small image
        new_image_size = 
            multi_image->size[0] * multi_image->size[1] * multi_image->pixel_size;
        data = malloc(new_image_size);
        CHKMEM(data);
        multi_image->small_image = acr_create_element(group_id, element_id,
                                                      acr_get_element_vr(multi_image->big_image),
                                                      new_image_size, data);
        acr_set_element_vr(multi_image->small_image,
                           acr_get_element_vr(multi_image->big_image));
        acr_set_element_byte_order(multi_image->small_image,
                                   acr_get_element_byte_order(multi_image->big_image));
        acr_set_element_vr_encoding(multi_image->small_image,
                                    acr_get_element_vr_encoding(multi_image->big_image));
        acr_insert_element_into_group_list(&group_list, 
                                           multi_image->small_image);
    }

    /* Return number of sub-images in this image */
    return multi_image->sub_images;
}

static int 
multi_image_modify_group_list(Acr_Group group_list, Multi_Image *multi_image,
                              int iimage, int load_image)
{
    int irow;
    int ibyte;
    int idim;
    int nbyte;
    int isub;
    int jsub;
    char *new;
    char *old;
    long old_offset;
    long new_offset;
    double position[3];
    string_t string;
    int islice;

    /* For interleaved sequences, we have to map the odd slices to the 
     * range slice_count/2..slice_count-1 and the even slices from
     * zero to slice_count/2-1
     */
    if (multi_image->mosaic_seq == MOSAIC_SEQ_INTERLEAVED) {
        if (iimage & 1) {       /* Odd?? */
            islice = (multi_image->slice_count / 2) + (iimage / 2);
        }
        else {
            islice = iimage / 2;
        }
    }
    else {
        islice = iimage;
    }

    // Check the image number 
    if ((iimage < 0) || (iimage > multi_image->sub_images)) {
        fprintf(stderr, "Invalid image number to send: %d of %d\n",
                iimage, multi_image->sub_images);
        exit(EXIT_FAILURE);
    }

    // Update the index
    acr_insert_numeric(&group_list, SPI_Current_slice_number, 
                       (double) iimage);

    // Update the position

    for (idim = 0; idim < 3; idim++) {
        position[idim] = multi_image->position[idim] + 
            (double) iimage * multi_image->step[idim];
    }

    sprintf(string, "%.15g\\%.15g\\%.15g",
            position[0], position[1], position[2]);
    acr_insert_string(&group_list, SPI_Image_position, string);
    acr_insert_string(&group_list, ACR_Image_position_patient, string);

    if (G.file_type == IMA || G.file_type == N3DCM) {
        // this will convert Siemens SPI info into dicom compliant info
        // not needed for Numaris 4
        update_coordinate_info(group_list);
    }

    if (load_image) {
        // Figure out the sub-image indices 
        isub = islice % multi_image->grid[0];
        jsub = islice / multi_image->grid[0];

        // Get pointers
        old = acr_get_element_data(multi_image->big_image);
        new = acr_get_element_data(multi_image->small_image);

        // Copy the image
        nbyte = multi_image->size[0] * multi_image->pixel_size;
        for (irow=0; irow < multi_image->size[1]; irow++) {
            old_offset = isub * multi_image->size[0] +
                (jsub * multi_image->size[1] + irow) * multi_image->big[0];
            old_offset *= multi_image->pixel_size;
            new_offset = (irow * multi_image->size[0]) * multi_image->pixel_size;
            for (ibyte=0; ibyte < nbyte; ibyte++) {
                new[new_offset + ibyte] = old[old_offset + ibyte];
            }
        }

        // Reset the byte order and VR encoding. This will be modified on each
        // send according to what the connection needs.
        acr_set_element_byte_order(multi_image->small_image,
                                   acr_get_element_byte_order(multi_image->big_image));
        acr_set_element_vr_encoding(multi_image->small_image,
                                    acr_get_element_vr_encoding(multi_image->big_image));
    }
    return 1;

}

static void 
multi_image_cleanup(Acr_Group group_list, Multi_Image *multi_image)
{
    if (multi_image->packed && multi_image->big_image != NULL) {
        acr_delete_element(multi_image->big_image);
    }
}

