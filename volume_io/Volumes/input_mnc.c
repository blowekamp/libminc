/* ----------------------------------------------------------------------------
@COPYRIGHT  :
              Copyright 1993,1994,1995 David MacDonald,
              McConnell Brain Imaging Centre,
              Montreal Neurological Institute, McGill University.
              Permission to use, copy, modify, and distribute this
              software and its documentation for any purpose and without
              fee is hereby granted, provided that the above copyright
              notice appear in all copies.  The author and McGill University
              make no representations about the suitability of this
              software for any purpose.  It is provided "as is" without
              express or implied warranty.
---------------------------------------------------------------------------- */

#include  <internal_volume_io.h>
#include  <minc.h>

#ifndef lint
static char rcsid[] = "$Header: /private-cvsroot/minc/volume_io/Volumes/input_mnc.c,v 1.46 1995-09-19 13:45:35 david Exp $";
#endif

#define  INVALID_AXIS   -1

#define  MIN_SLAB_SIZE    10000     /* at least 10000 entries per read */
#define  MAX_SLAB_SIZE   400000     /* no more than 200 K at a time */

private  BOOLEAN  match_dimension_names(
    int               n_volume_dims,
    char              *volume_dimension_names[],
    int               n_file_dims,
    char              *file_dimension_names[],
    int               to_volume_index[] );

/* ----------------------------- MNI Header -----------------------------------
@NAME       : initialize_minc_input_from_minc_id
@INPUT      : minc_id
              volume
              options
@OUTPUT     : 
@RETURNS    : Minc_file
@DESCRIPTION: Initializes input of volumes from an already opened MINC file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  Minc_file  initialize_minc_input_from_minc_id(
    int                  minc_id,
    Volume               volume,
    minc_input_options   *options )
{
    minc_file_struct    *file;
    int                 dim_vars[MAX_VAR_DIMS], n_vol_dims;
    int                 i, slab_size, length, prev_sizes[MAX_VAR_DIMS];
    nc_type             prev_nc_type;
    BOOLEAN             different;
    BOOLEAN             min_voxel_found, max_voxel_found, range_specified;
    double              valid_range[2], temp;
    long                long_size, mindex[MAX_VAR_DIMS];
    BOOLEAN             converted_sign;
    nc_type             converted_type;
    STRING              signed_flag;
    nc_type             file_datatype;
    int                 sizes[MAX_VAR_DIMS];
    double              file_separations[MAX_VAR_DIMS];
    Real                volume_separations[MI_NUM_SPACE_DIMS];
    Real                default_voxel_min, default_voxel_max;
    Real                world_space[N_DIMENSIONS];
    double              start_position[MAX_VAR_DIMS];
    double              dir_cosines[MAX_VAR_DIMS][MI_NUM_SPACE_DIMS];
    double              tmp_cosines[MI_NUM_SPACE_DIMS];
    BOOLEAN             spatial_dim_flags[MAX_VAR_DIMS];
    Vector              offset;
    Point               origin;
    Real                zero_voxel[MAX_DIMENSIONS];
    Vector              spatial_axis;
    double              real_min, real_max;
    int                 d, dimvar, which_valid_axis, axis;
    int                 spatial_axis_indices[MAX_VAR_DIMS];
    minc_input_options  default_options;
    BOOLEAN             no_volume_data_type;

    ALLOC( file, 1 );

    file->cdfid = minc_id;
    file->file_is_being_read = TRUE;
    file->volume = volume;

    if( options == (minc_input_options *) NULL )
    {
        set_default_minc_input_options( &default_options );
        set_default_minc_input_options( &file->original_input_options );
        options = &default_options;
    }
    else
        file->original_input_options = *options;

    get_volume_sizes( volume, prev_sizes );
    prev_nc_type = volume->nc_data_type;

    /* --- find the image variable */

    file->img_var = ncvarid( file->cdfid, MIimage );

    ncvarinq( file->cdfid, file->img_var, (char *) NULL, &file_datatype,
              &file->n_file_dimensions, dim_vars, (int *) NULL );

    for_less( d, 0, file->n_file_dimensions )
    {
        ALLOC( file->dim_names[d], MAX_STRING_LENGTH + 1 );

        (void) ncdiminq( file->cdfid, dim_vars[d], file->dim_names[d],
                         &long_size );
        file->sizes_in_file[d] = (int) long_size;
    }

    file->converting_to_colour = FALSE;

    if( strcmp( file->dim_names[file->n_file_dimensions-1],
                MIvector_dimension ) == 0 )
    {
        if( options->convert_vector_to_colour_flag &&
            options->dimension_size_for_colour_data ==
                                file->sizes_in_file[file->n_file_dimensions-1] )
        {
            for_less( i, 0, 4 )
            {
                if( options->rgba_indices[i] >=
                    options->dimension_size_for_colour_data )
                {
                    print_error( "Error: rgba indices out of range.\n" );
                    FREE( file );
                    return( (Minc_file) 0 );
                }
                file->rgba_indices[i] = options->rgba_indices[i];
            }

            set_volume_type( volume, NC_LONG, FALSE, 0.0, 0.0 );
            volume->is_rgba_data = TRUE;
            file->converting_to_colour = TRUE;
            FREE( file->dim_names[file->n_file_dimensions-1] );
            --file->n_file_dimensions;
        }
        else if( options->convert_vector_to_scalar_flag )
        {
            FREE( file->dim_names[file->n_file_dimensions-1] );
            --file->n_file_dimensions;
        }
    }

    n_vol_dims = get_volume_n_dimensions( volume );

    if( file->n_file_dimensions < n_vol_dims )
    {
        print_error( "Error: MINC file has only %d dims, volume requires %d.\n",
               file->n_file_dimensions, n_vol_dims );
        FREE( file );
        return( (Minc_file) 0 );
    }
    else if( file->n_file_dimensions > MAX_VAR_DIMS )
    {
        print_error( "Error: MINC file has %d dims, can only handle %d.\n",
               file->n_file_dimensions, MAX_VAR_DIMS );
        FREE( file );
        return( (Minc_file) NULL );
    }

    /* --- match the dimension names of the volume with those in the file */

    if( !match_dimension_names( get_volume_n_dimensions(volume),
                                volume->dimension_names,
                                file->n_file_dimensions, file->dim_names,
                                file->to_volume_index ) )
    {
        print_error( "Error:  dimension names did not match: \n" );
        
        print_error( "\n" );
        print_error( "Requested:\n" );
        for_less( d, 0, n_vol_dims )
            print_error( "%d: %s\n", d+1, volume->dimension_names[d] );

        print_error( "\n" );
        print_error( "In File:\n" );
        for_less( d, 0, file->n_file_dimensions )
            print_error( "%d: %s\n", d+1, file->dim_names[d] );

        FREE( file );
        return( (Minc_file) NULL );
    }

    for_less( d, 0, n_vol_dims )
        file->to_file_index[d] = INVALID_AXIS;

    for_less( d, 0, file->n_file_dimensions )
    {
        if( file->to_volume_index[d] != INVALID_AXIS )
            file->to_file_index[file->to_volume_index[d]] = d;
    }

    file->n_volumes_in_file = 1;

    /* --- find the spatial axes (x,y,z) */

    which_valid_axis = 0;

    for_less( d, 0, N_DIMENSIONS )
    {
        volume->spatial_axes[d] = INVALID_AXIS;
        file->spatial_axes[d] = INVALID_AXIS;
    }

    for_less( d, 0, file->n_file_dimensions )
    {
        if( convert_dim_name_to_spatial_axis( file->dim_names[d], &axis ) )
        {
            spatial_axis_indices[d] = axis;
            file->spatial_axes[axis] = d;
        }
        else
            spatial_axis_indices[d] = INVALID_AXIS;

        spatial_dim_flags[d] = (spatial_axis_indices[d] != INVALID_AXIS);

        if( file->to_volume_index[d] != INVALID_AXIS )
        {
            file->valid_file_axes[which_valid_axis] = d;

            if( spatial_dim_flags[d] )
            {
                volume->spatial_axes[spatial_axis_indices[d]] =
                                        file->to_volume_index[d];
            }

            ++which_valid_axis;
        }
    }

    /* --- get the spatial axis info, slice separation, start pos, etc. */

    for_less( d, 0, file->n_file_dimensions )
    {
        file_separations[d] = 1.0;
        start_position[d] = 0.0;

        if( spatial_dim_flags[d] )
        {
            dir_cosines[d][0] = 0.0;
            dir_cosines[d][1] = 0.0;
            dir_cosines[d][2] = 0.0;
            dir_cosines[d][spatial_axis_indices[d]] = 1.0;
        }

        dimvar = ncvarid( file->cdfid, file->dim_names[d] );
        if( dimvar != MI_ERROR )
        {
            (void) miattget1( file->cdfid, dimvar, MIstep, NC_DOUBLE,
                              (void *) (&file_separations[d]) );

            if( spatial_dim_flags[d] )
            {
                if( miattget1( file->cdfid, dimvar, MIstart, NC_DOUBLE,
                               (void *) (&start_position[d]) ) == MI_ERROR )
                    start_position[d] = 0.0;

                if( miattget( file->cdfid, dimvar, MIdirection_cosines,
                                 NC_DOUBLE, MI_NUM_SPACE_DIMS,
                                 (void *) tmp_cosines, (int *) NULL )
                     != MI_ERROR )
                {
                    dir_cosines[d][0] = tmp_cosines[0];
                    dir_cosines[d][1] = tmp_cosines[1];
                    dir_cosines[d][2] = tmp_cosines[2];
                }
            }
        }

        if( file->to_volume_index[d] == INVALID_AXIS )
        {
            file->n_volumes_in_file *= file->sizes_in_file[d];
        }
        else
        {
            sizes[file->to_volume_index[d]] = file->sizes_in_file[d];
            volume_separations[file->to_volume_index[d]] =
                                          file_separations[d];
        }
    }

    /* --- create the file world transform */

    fill_Point( origin, 0.0, 0.0, 0.0 );

    for_less( d, 0, MAX_DIMENSIONS )
        zero_voxel[d] = 0.0;

    for_less( d, 0, N_DIMENSIONS )
    {
        axis = file->spatial_axes[d];
        if( axis != INVALID_AXIS )
        {
            fill_Vector( spatial_axis,
                         dir_cosines[axis][0],
                         dir_cosines[axis][1],
                         dir_cosines[axis][2] );
            NORMALIZE_VECTOR( spatial_axis, spatial_axis );
            
            SCALE_VECTOR( offset, spatial_axis, start_position[axis] );
            ADD_POINT_VECTOR( origin, origin, offset );
        }
    }

    world_space[X] = Point_x(origin);
    world_space[Y] = Point_y(origin);
    world_space[Z] = Point_z(origin);

    compute_world_transform( file->spatial_axes, file_separations,
                             zero_voxel, world_space, dir_cosines,
                             &file->voxel_to_world_transform );

    /* --- create the world transform stored in the volume */

    fill_Point( origin, 0.0, 0.0, 0.0 );

    for_less( d, 0, file->n_file_dimensions )
    {
        if( file->to_volume_index[d] != INVALID_AXIS )
        {
            set_volume_direction_cosine( volume,
                                         file->to_volume_index[d],
                                         dir_cosines[d] );
        }
    }

    general_transform_point( &file->voxel_to_world_transform,
                             0.0, 0.0, 0.0,
                             &world_space[X], &world_space[Y], &world_space[Z]);

    for_less( d, 0, N_DIMENSIONS )
        zero_voxel[d] = 0.0;

    set_volume_translation( volume, zero_voxel, world_space );
    set_volume_separations( volume, volume_separations );

    /* --- decide on type conversion */

    if( file->converting_to_colour )
    {
        converted_type = NC_FLOAT;
        converted_sign = FALSE;
    }
    else
    {
        no_volume_data_type = (get_volume_data_type(volume) == NO_DATA_TYPE);
        if( no_volume_data_type )     /* --- use type of file */
        {
            if( miattgetstr( file->cdfid, file->img_var, MIsigntype,
                             MAX_STRING_LENGTH, signed_flag ) != (char *) NULL )
            {
                converted_sign = (strcmp( signed_flag, MI_SIGNED ) == 0);
            }
            else
                converted_sign = file_datatype != NC_BYTE;
    
            converted_type = file_datatype;
            set_volume_type( volume, converted_type, converted_sign, 0.0, 0.0 );
        }
        else                                        /* --- use specified type */
        {
            converted_type = get_volume_nc_data_type( volume, &converted_sign );
        }
    }

    set_volume_sizes( volume, sizes );

    for_less( d, 0, file->n_file_dimensions )
        mindex[d] = 0;

    /* --- create the image conversion variable */

    file->minc_icv = miicv_create();

    (void) miicv_setint( file->minc_icv, MI_ICV_TYPE, converted_type );
    (void) miicv_setstr( file->minc_icv, MI_ICV_SIGN,
                         converted_sign ? MI_SIGNED : MI_UNSIGNED );
    (void) miicv_setint( file->minc_icv, MI_ICV_DO_NORM, TRUE );
    (void) miicv_setint( file->minc_icv, MI_ICV_DO_FILLVALUE, TRUE );

    get_volume_voxel_range( volume, &valid_range[0], &valid_range[1] );
    range_specified = (valid_range[0] < valid_range[1]);

    max_voxel_found = FALSE;
    min_voxel_found = FALSE;

    valid_range[0] = 0.0;
    valid_range[1] = 0.0;

    if( file->converting_to_colour )
    {
        min_voxel_found = TRUE;
        max_voxel_found = TRUE;
        valid_range[0] = 0.0;
        valid_range[1] = 2.0 * (double) (1ul << 31ul) - 1.0;
        set_volume_voxel_range( volume, valid_range[0], valid_range[1] );
    }
    else if( no_volume_data_type )
    {
        if( miattget( file->cdfid, file->img_var, MIvalid_range, NC_DOUBLE,
                         2, (void *) valid_range, &length ) == MI_ERROR ||
            length != 2 )
        {
            if( miattget1( file->cdfid, file->img_var, MIvalid_min, NC_DOUBLE,
                           (void *) &valid_range[0] ) != MI_ERROR )
            {
                min_voxel_found = TRUE;
            }
            if( miattget1( file->cdfid, file->img_var, MIvalid_max, NC_DOUBLE,
                           (void *) &valid_range[1] ) != MI_ERROR )
            {
                max_voxel_found = TRUE;
            }
        }
        else
        {
            if( valid_range[0] > valid_range[1] )
            {
                temp = valid_range[0];
                valid_range[0] = valid_range[1];
                valid_range[1] = temp;
            }
            min_voxel_found = TRUE;
            max_voxel_found = TRUE;
        }
    }

    if( !file->converting_to_colour &&
        (no_volume_data_type || !range_specified) )
    {
        set_volume_voxel_range( volume, 0.0, 0.0 );
        get_volume_voxel_range( volume, &default_voxel_min, &default_voxel_max);

        if( min_voxel_found && max_voxel_found )
            set_volume_voxel_range( volume, valid_range[0], valid_range[1] );
        else if( min_voxel_found && !max_voxel_found )
            set_volume_voxel_range( volume, valid_range[0], default_voxel_max );
        else if( !min_voxel_found && max_voxel_found )
            set_volume_voxel_range( volume, default_voxel_min, valid_range[0] );
    }

    if( !file->converting_to_colour )
    {
        get_volume_voxel_range( volume, &valid_range[0], &valid_range[1] );

        (void) miicv_setdbl( file->minc_icv, MI_ICV_VALID_MIN, valid_range[0]);
        (void) miicv_setdbl( file->minc_icv, MI_ICV_VALID_MAX, valid_range[1]);
    }
    else
    {
        (void) miicv_setdbl( file->minc_icv, MI_ICV_VALID_MIN, 0.0 );
        (void) miicv_setdbl( file->minc_icv, MI_ICV_VALID_MAX, 1.0 );
    }

    if( options->convert_vector_to_scalar_flag && !file->converting_to_colour )
    {
        (void) miicv_setint( file->minc_icv, MI_ICV_DO_DIM_CONV, TRUE );
        (void) miicv_setint( file->minc_icv, MI_ICV_DO_SCALAR, TRUE );
        (void) miicv_setint( file->minc_icv, MI_ICV_XDIM_DIR, FALSE );
        (void) miicv_setint( file->minc_icv, MI_ICV_YDIM_DIR, FALSE );
        (void) miicv_setint( file->minc_icv, MI_ICV_ZDIM_DIR, FALSE );
        (void) miicv_setint( file->minc_icv, MI_ICV_KEEP_ASPECT, FALSE );
    }

    (void) miicv_attach( file->minc_icv, file->cdfid, file->img_var );

    /* --- compute the mapping to real values */

    if( !file->converting_to_colour )
    {
         (void) miicv_inqdbl( file->minc_icv, MI_ICV_NORM_MIN, &real_min );
         (void) miicv_inqdbl( file->minc_icv, MI_ICV_NORM_MAX, &real_max );

         set_volume_real_range( volume, real_min, real_max );
    }

    if( options->promote_invalid_to_min_flag )
    {
        if( !file->converting_to_colour )
            (void) miicv_setdbl( file->minc_icv, MI_ICV_FILLVALUE, valid_range[0] );
        else
            (void) miicv_setdbl( file->minc_icv, MI_ICV_FILLVALUE, 0.0 );
    }

    for_less( d, 0, file->n_file_dimensions )
        file->indices[d] = 0;

    file->end_volume_flag = FALSE;

    ncopts = NC_VERBOSE | NC_FATAL;

    /* --- decide how many dimensions to read in at a time */

    file->n_slab_dims = 0;
    slab_size = 1;
    d = file->n_file_dimensions-1;
    
    do
    {
        if( file->to_volume_index[d] != INVALID_AXIS )
        {
            ++file->n_slab_dims;
            slab_size *= file->sizes_in_file[d];
        }
        --d;
    }
    while( d >= 0 && slab_size < MIN_SLAB_SIZE );

    if( slab_size > MAX_SLAB_SIZE && file->n_slab_dims > 1 )
    {
        --file->n_slab_dims;
    }

    /* --- decide whether the volume data must be freed (if it changed size) */

    different = FALSE;
    for_less( d, 0, n_vol_dims )
    {
        if( sizes[d] != prev_sizes[d] )
            different = TRUE;
    }

    if( prev_nc_type != converted_type )
        different = TRUE;

    if( different && volume_is_alloced( volume ) )
        free_volume_data( volume );

    return( file );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : initialize_minc_input
@INPUT      : filename
              volume
@OUTPUT     : 
@RETURNS    : Minc_file
@DESCRIPTION: Initializes the input of a MINC file, passing back a MINC
              file pointer.  It assumes that the volume has been created,
              with the desired type, or NC_UNSPECIFIED type if it is desired
              to use whatever type is in the file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June, 1993           David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  Minc_file  initialize_minc_input(
    char                 filename[],
    Volume               volume,
    minc_input_options   *options )
{
    Minc_file    file;
    int          minc_id;

    ncopts = 0;
    minc_id = miopen( filename, NC_NOWRITE );

    if( minc_id == MI_ERROR )
    {
        print_error( "Error: opening MINC file \"%s\".\n", filename );
        return( (Minc_file) 0 );
    }

    file = initialize_minc_input_from_minc_id( minc_id, volume, options );

    if( file == (Minc_file) NULL )
        (void) miclose( minc_id );
    else
        (void) strcpy( file->filename, filename );

    return( file );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : get_n_input_volumes
@INPUT      : file
@OUTPUT     : 
@RETURNS    : number of input volumes
@DESCRIPTION: After initializing the file input with a specified volume,
              the user calls this function to decide how many volumes are
              stored in the file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June, 1993           David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  int  get_n_input_volumes(
    Minc_file  file )
{
    return( file->n_volumes_in_file );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : close_minc_input
@INPUT      : file
@OUTPUT     : 
@RETURNS    : OK or ERROR
@DESCRIPTION: Closes the minc input file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June, 1993           David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  Status  close_minc_input(
    Minc_file   file )
{
    int  d;

    if( file == (Minc_file) NULL )
    {
        print_error( "close_minc_input(): NULL file.\n" );
        return( ERROR );
    }

    (void) miclose( file->cdfid );
    (void) miicv_free( file->minc_icv );

    for_less( d, 0, file->n_file_dimensions )
        FREE( file->dim_names[d] );

    delete_general_transform( &file->voxel_to_world_transform );
    FREE( file );

    return( OK );
}

public  Status  input_minc_hyperslab(
    Minc_file        file,
    BOOLEAN          one_d_array_flag,
    multidim_array   *array,
    int              array_start[],
    int              to_array[],
    int              start[],
    int              count[] )
{
    Status           status;
    int              ind, expected_ind, n_array_dims, file_ind, d, i, dim;
    int              size0, size1, size2, size3, size4;
    int              used_array_start[MAX_DIMENSIONS];
    int              n_tmp_dims, n_file_dims;
    void             *void_ptr;
    BOOLEAN          direct_to_array, non_full_size_found;
    int              tmp_ind, tmp_sizes[MAX_VAR_DIMS];
    int              vol1_indices[MAX_DIMENSIONS];
    int              zero[MAX_VAR_DIMS];
    int              v[MAX_DIMENSIONS], voxel[MAX_DIMENSIONS];
    long             used_start[MAX_VAR_DIMS], used_count[MAX_VAR_DIMS];
    Real             rgb[4];
    Colour           colour;
    multidim_array   buffer_array, rgb_array, *array_to_read;
    Data_types       data_type;

    n_array_dims = get_multidim_n_dimensions( array );
    n_file_dims = file->n_file_dimensions;

    direct_to_array = TRUE;

    expected_ind = n_array_dims-1;
    tmp_ind = n_file_dims-1;
    non_full_size_found = FALSE;

    for_less( ind, 0, n_array_dims )
        vol1_indices[ind] = -1;

    for( file_ind = n_file_dims-1;  file_ind >= 0;  --file_ind )
    {
        used_start[file_ind] = (long) start[file_ind];
        used_count[file_ind] = (long) count[file_ind];

        ind = to_array[file_ind];

        if( !one_d_array_flag && ind != INVALID_AXIS )
        {
            if( !non_full_size_found &&
                count[file_ind] < file->sizes_in_file[file_ind] )
                non_full_size_found = TRUE;
            else if( non_full_size_found && count[file_ind] > 1 )
                direct_to_array = FALSE;

            if( count[file_ind] > 1 && ind != expected_ind )
                direct_to_array = FALSE;

            if( count[file_ind] != 1 || file->sizes_in_file[file_ind] == 1 )
            {
                tmp_sizes[tmp_ind] = count[file_ind];
                vol1_indices[tmp_ind] = ind;
                --tmp_ind;
            }

            --expected_ind;
        }
    }

    if( !direct_to_array || file->converting_to_colour )
    {
        if( one_d_array_flag )
        {
            n_tmp_dims = 1;
            get_multidim_sizes( array, tmp_sizes );
            vol1_indices[0] = 0;
            zero[0] = 0;
        }
        else
        {
            n_tmp_dims = n_file_dims - tmp_ind - 1;
            for_less( dim, 0, n_tmp_dims )
            {
                tmp_sizes[dim] = tmp_sizes[dim+tmp_ind+1];
                vol1_indices[dim] = vol1_indices[dim+tmp_ind+1];
                zero[dim] = 0;
            }
        }

        data_type = get_multidim_data_type( array );
        create_multidim_array( &buffer_array, n_tmp_dims, tmp_sizes, data_type);

        if( file->converting_to_colour )
        {
            used_start[n_file_dims] = 0;
            used_count[n_file_dims] = file->sizes_in_file[n_file_dims];
            tmp_sizes[n_tmp_dims] = (int) used_count[n_file_dims];

            create_multidim_array( &rgb_array, n_tmp_dims+1, tmp_sizes, FLOAT );

            array_to_read = &rgb_array;
        }
        else
            array_to_read = &buffer_array;

        for_less( dim, 0, n_tmp_dims+1 )
            used_array_start[dim] = 0;
    }
    else
    {
        array_to_read = array;
        for_less( dim, 0, n_array_dims )
            used_array_start[dim] = array_start[dim];
    }

    GET_MULTIDIM_PTR( void_ptr, *array_to_read,
                      used_array_start[0], used_array_start[1],
                      used_array_start[2], used_array_start[3],
                      used_array_start[4] );

    if( miicv_get( file->minc_icv, used_start, used_count, void_ptr ) ==
                                 MI_ERROR )
    {
        status = ERROR;
        if( file->converting_to_colour )
            delete_multidim_array( &rgb_array );
        if( !direct_to_array || file->converting_to_colour )
            delete_multidim_array( &buffer_array );
    }
    else
        status = OK;

    if( status == OK && (!direct_to_array || file->converting_to_colour) )
    {
        if( file->converting_to_colour )
        {
            for_less( dim, n_tmp_dims, MAX_DIMENSIONS )
                tmp_sizes[dim] = 1;           

            size0 = tmp_sizes[0];
            size1 = tmp_sizes[1];
            size2 = tmp_sizes[2];
            size3 = tmp_sizes[3];
            size4 = tmp_sizes[4];

            for_less( v[4], 0, size4 )
            for_less( v[3], 0, size3 )
            for_less( v[2], 0, size2 )
            for_less( v[1], 0, size1 )
            for_less( v[0], 0, size0 )
            {
                for_less( d, 0, n_tmp_dims )
                    voxel[d] = v[d];

                for_less( i, 0, 4 )
                {
                    if( file->rgba_indices[i] < 0 )
                    {
                        if( i < 3 )
                            rgb[i] = 0.0;
                        else
                            rgb[i] = 1.0;
                    }
                    else
                    {
                         voxel[n_tmp_dims] = file->rgba_indices[i];
                         GET_MULTIDIM( rgb[i], rgb_array,
                                       voxel[0], voxel[1],
                                       voxel[2], voxel[3], voxel[4] );
                    }
                }

                colour = make_rgba_Colour_0_1( rgb[0], rgb[1], rgb[2], rgb[3] );
                SET_MULTIDIM( buffer_array,
                              voxel[0], voxel[1],
                              voxel[2], voxel[3], voxel[4], colour );
            }

            delete_multidim_array( &rgb_array );
        }

        copy_multidim_reordered( array, array_start,
                                 &buffer_array, zero, tmp_sizes, vol1_indices );

        delete_multidim_array( &buffer_array );
    }

    return( status );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : input_slab
@INPUT      : file
              volume
              start
              count
@OUTPUT     : 
@RETURNS    : 
@DESCRIPTION: Inputs a multidimensional slab from the file and copies it
              into the appropriate part of the volume.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June, 1993           David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

private  void  input_slab(
    Minc_file   file,
    Volume      volume,
    int         to_volume[],
    long        start[],
    long        count[] )
{
    int      file_ind, ind;
    int      volume_start[MAX_VAR_DIMS];
    int      file_start[MAX_DIMENSIONS];
    int      file_count[MAX_DIMENSIONS];

    for_less( file_ind, 0, file->n_file_dimensions )
    {
        file_start[file_ind] = (int) start[file_ind];
        file_count[file_ind] = (int) count[file_ind];

        ind = to_volume[file_ind];
        if( ind != INVALID_AXIS )
            volume_start[ind] = file_start[file_ind];
        else
            volume_start[ind] = 0;
    }

    (void) input_minc_hyperslab( file, FALSE,
                                 &volume->array, volume_start, to_volume,
                                 file_start, file_count );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : input_more_minc_file
@INPUT      : file
@OUTPUT     : fraction_done        - amount of file read
@RETURNS    : TRUE if volume has more left to read
@DESCRIPTION: Reads another chunk from the input file, passes back the
              total fraction read so far, and returns FALSE when the whole
              volume has been read.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June, 1993           David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  BOOLEAN  input_more_minc_file(
    Minc_file   file,
    Real        *fraction_done )
{
    int      d, ind, n_done, total, n_slab;
    long     count[MAX_VAR_DIMS];
    Volume   volume;
    BOOLEAN  increment;

    if( file->end_volume_flag )
    {
        print_error( "End of file in input_more_minc_file()\n" );
        return( FALSE );
    }

    volume = file->volume;

    if( !volume_is_alloced( volume ) )
    {
        alloc_volume_data( volume );
        if( volume->is_cached_volume )
        {
            open_cache_volume_input_file( &volume->cache, volume,
                                          file->filename,
                                          &file->original_input_options );
        }
    }

    if( volume->is_cached_volume )
    {
        *fraction_done = 1.0;
        file->end_volume_flag = TRUE;
    }
    else
    {
        /* --- set the counts for reading, actually these will be the same
               every time */

        for_less( ind, 0, file->n_file_dimensions )
            count[ind] = 1;

        n_slab = 0;

        for( d = file->n_file_dimensions-1;
             d >= 0 && n_slab < file->n_slab_dims;
             --d )
        {
            if( file->to_volume_index[d] != INVALID_AXIS )
            {
                count[d] = file->sizes_in_file[d];
                ++n_slab;
            }
        }

        input_slab( file, volume, file->to_volume_index, file->indices, count );

        /* --- advance to next slab */

        increment = TRUE;
        n_slab = 0;
        total = 1;
        n_done = 0;

        for( d = file->n_file_dimensions-1;  d >= 0;  --d )
        {
            if( n_slab >= file->n_slab_dims &&
                file->to_volume_index[d] != INVALID_AXIS )
            {
                if( increment )
                {
                    ++file->indices[d];
                    if( file->indices[d] < file->sizes_in_file[d] )
                        increment = FALSE;
                    else
                        file->indices[d] = 0;
                }
                n_done += total * file->indices[d];
                total *= file->sizes_in_file[d];
            }

            if( file->to_volume_index[d] != INVALID_AXIS )
                ++n_slab;
        }

        if( increment )
        {
            *fraction_done = 1.0;
            file->end_volume_flag = TRUE;
        }
        else
        {
            *fraction_done = (Real) n_done / (Real) total;
        }
    }

    return( !file->end_volume_flag );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : advance_input_volume
@INPUT      : file
@OUTPUT     : 
@RETURNS    : TRUE if more volumes to read
@DESCRIPTION: Advances the file indices to prepare for reading the next
              volume from the file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  BOOLEAN  advance_input_volume(
    Minc_file   file )
{
    int   ind, c, axis;
    Real  voxel[MAX_DIMENSIONS], world_space[N_DIMENSIONS];

    ind = file->n_file_dimensions-1;

    while( ind >= 0 )
    {
        if( file->to_volume_index[ind] == INVALID_AXIS )
        {
            ++file->indices[ind];
            if( file->indices[ind] < file->sizes_in_file[ind] )
                break;

            file->indices[ind] = 0;
        }
        --ind;
    }

    if( ind >= 0 )
    {
        file->end_volume_flag = FALSE;

        for_less( ind, 0, get_volume_n_dimensions( file->volume ) )
            file->indices[file->valid_file_axes[ind]] = 0;

        for_less( c, 0, N_DIMENSIONS )
        {
            axis = file->spatial_axes[c];
            if( axis != INVALID_AXIS )
                voxel[c] = file->indices[axis];
            else
                voxel[c] = 0.0;
        }

        general_transform_point( &file->voxel_to_world_transform,
                                 voxel[0], voxel[1], voxel[2],
                                 &world_space[X], &world_space[Y],
                                 &world_space[Z]);

        for_less( c, 0, get_volume_n_dimensions(file->volume) )
            voxel[c] = 0.0;

        set_volume_translation( file->volume, voxel, world_space );

        if( file->volume->is_cached_volume )
            set_cache_volume_file_offset( &file->volume->cache, file->volume,
                                          file->indices );
    }
    else
        file->end_volume_flag = TRUE;

    return( file->end_volume_flag );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : reset_input_volume
@INPUT      : file
@OUTPUT     : 
@RETURNS    : 
@DESCRIPTION: Rewinds the file indices to start inputting volumes from the
              file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  reset_input_volume(
    Minc_file   file )
{
    int   d;

    for_less( d, 0, file->n_file_dimensions )
        file->indices[d] = 0;
    file->end_volume_flag = FALSE;

    set_cache_volume_file_offset( &file->volume->cache, file->volume,
                                  file->indices );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : match_dimension_names
@INPUT      : n_volume_dims
              volume_dimension_names
              n_file_dims
              file_dimension_names
@OUTPUT     : to_volume_index
@RETURNS    : TRUE if match found
@DESCRIPTION: Attempts to match all the volume dimensions with the file
              dimensions.  This is done in 3 passes.  In the first pass,
              exact matches are found.  In the second pass, volume dimensions
              of "any_spatial_dimension" are matched.  On the final pass,
              volume dimension names which are empty strings are matched
              to any remaining file dimensions.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

private  BOOLEAN  match_dimension_names(
    int               n_volume_dims,
    char              *volume_dimension_names[],
    int               n_file_dims,
    char              *file_dimension_names[],
    int               to_volume_index[] )
{
    int      i, j, iteration, n_matches, dummy;
    BOOLEAN  match;
    BOOLEAN  volume_dim_found[MAX_DIMENSIONS];

    n_matches = 0;

    for_less( i, 0, n_file_dims )
        to_volume_index[i] = INVALID_AXIS;

    for_less( i, 0, n_volume_dims )
        volume_dim_found[i] = FALSE;

    for_less( iteration, 0, 3 )
    {
        for( i = n_volume_dims-1;  i >= 0;  --i )
        {
            if( !volume_dim_found[i] )
            {
                for( j = n_file_dims-1;  j >= 0;  --j )
                {
                    if( to_volume_index[j] == INVALID_AXIS )
                    {
                        switch( iteration )
                        {
                        case 0:
                            match = strcmp( volume_dimension_names[i],
                                            file_dimension_names[j] ) == 0;
                            break;
                        case 1:
                            match = (strcmp( volume_dimension_names[i],
                                             ANY_SPATIAL_DIMENSION ) == 0) &&
                                convert_dim_name_to_spatial_axis(
                                     file_dimension_names[j], &dummy );
                            break;
                        case 2:
                            match = (strlen(volume_dimension_names[i]) == 0);
                            break;
                        }

                        if( match )
                        {
                            to_volume_index[j] = i;
                            volume_dim_found[i] = TRUE;
                            ++n_matches;
                            break;
                        }
                    }
                }
            }
        }
    }

    return( n_matches == n_volume_dims );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : get_minc_file_id
@INPUT      : file
@OUTPUT     : 
@RETURNS    : minc file id
@DESCRIPTION: Returns the minc file id to allow user to perform MINC calls on
              this file.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  int  get_minc_file_id(
    Minc_file  file )
{
    return( file->cdfid );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_default_minc_input_options
@INPUT      : 
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the default minc input options.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_default_minc_input_options(
    minc_input_options  *options )
{
    static  int     default_rgba_indices[4] = { 0, 1, 2, -1 };

    set_minc_input_promote_invalid_to_min_flag( options, TRUE );
    set_minc_input_vector_to_scalar_flag( options, TRUE );
    set_minc_input_vector_to_colour_flag( options, FALSE );
    set_minc_input_colour_dimension_size( options, 3 );
    set_minc_input_colour_indices( options, default_rgba_indices );
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_minc_input_promote_invalid_to_min_flag
@INPUT      : flag
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the invalid promotion flag of the input options.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_minc_input_promote_invalid_to_min_flag(
    minc_input_options  *options,
    BOOLEAN             flag )
{
    options->promote_invalid_to_min_flag = flag;
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_minc_input_vector_to_scalar_flag
@INPUT      : flag
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the vector conversion flag of the input options.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_minc_input_vector_to_scalar_flag(
    minc_input_options  *options,
    BOOLEAN             flag )
{
    options->convert_vector_to_scalar_flag = flag;
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_minc_input_vector_to_colour_flag
@INPUT      : flag
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the colour conversion flag of the input options.  Any
              volume with a vector dimension of length 3 will be converted
              to a 32 bit colour.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_minc_input_vector_to_colour_flag(
    minc_input_options  *options,
    BOOLEAN             flag )
{
    options->convert_vector_to_colour_flag = flag;
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_minc_input_colour_dimension_size
@INPUT      : size
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the required number of vector components in a file that
              contains colour data.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_minc_input_colour_dimension_size(
    minc_input_options  *options,
    int                 size )
{
    if( size > 0 )
        options->dimension_size_for_colour_data = size;
    else
    {
        print_error( "Warning: set_minc_input_colour_dimension_size:\n" );
        print_error( "         illegal size: %d\n", size );
    }
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : set_minc_input_colour_indices
@INPUT      : indices
@OUTPUT     : options
@RETURNS    : 
@DESCRIPTION: Sets the indices of the red, green, blue, and alpha in
              files that contain colours as the vector dimension.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : 1993            David MacDonald
@MODIFIED   : 
---------------------------------------------------------------------------- */

public  void  set_minc_input_colour_indices(
    minc_input_options  *options,
    int                 indices[4] )
{
    int   i;

    for_less( i, 0, 4 )
        options->rgba_indices[i] = indices[i];
}
