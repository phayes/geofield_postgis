<?php

$plugin = array(
  'title' => 'PostGIS',
  'schema' => 'geofield_postgis_backend_schema',
  'save' => 'geofield_postgis_backend_save',
  'types' => array('Point','LineString','Polygon','MultiPoint','MultiLineString','MultiPolygon','GeometryCollection'),
  'validate' => NULL,     //@@TODO: Additional validation callback. Takes a GeoPHP Geometry object.
  'update_field' => 'geofield_postgis_backend_update_field',
  'delete_field' => NULL,  // If we need to get rid of any additional data not on the field table.
  'requirements' => 'geofield_postgis_backend_requirements',
  'settings' => 'geofield_postgis_backend_settings',
);

function geofield_postgis_backend_schema($field) {
  //@@TODO: SRID
  $settings = $field['settings']['postgis']; // geometry or geography
  return array(
    'type' => 'blob',
    'pgsql_type' => $settings['fieldtype'],
    'not null' => FALSE,
  );
}

// This is when saving a value 
function geofield_postgis_backend_save($geometry) {
  $geometry = geoPHP::load($geometry);
  $unpacked = unpack('H*', $geometry->out('ewkb'));
  //@@TODO: cannot imagine how to get the settled srid on the field instance here
  //  I'm using incode 32648 transformation for a postgis point with srid 32648 - It works
  $query = db_query(
    'SELECT ST_SetSRID(:geom::geometry, 32648)',
        array(
          ':geom' => $unpacked[1],
        )
  );
  
  $new = $query->fetchObject();
  return $new->st_setsrid;
}

function geofield_postgis_backend_requirements(&$error) {
  try {
    $value = db_query('SELECT substring(PostGIS_Version() from 1 for 3)')->fetchField();
    if (empty($value)) {
      $error = 'Could not detect postGIS version';
      return FALSE;
    }

    $comparisson = version_compare($value, '1.5');
    if ($comparisson === FALSE) {
      $error = 'Could not detect postGIS version';
      return FALSE;
    }
    if ($comparisson == -1) {
      $error = 'PostGIS 1.5 or later required';
      return FALSE;
    }
    return TRUE;
  }
  catch (PDOException $e) {
    $error = 'Could not detect postGIS';
    return FALSE;
  }
}

function geofield_postgis_backend_settings($field, $instance, $has_data) {
  $form = array();
  $defaults = (isset($field['settings']['postgis'])) ? $field['settings']['postgis'] : array();
  
  $form['fieldtype'] = array(
    '#title' => 'PostGIS Type',
    '#type' => 'select',
    '#options' => array(
      'geometry' => 'Geometry',
    ),
    '#default_value' => isset($defaults['fieldtype']) ? $defaults['fieldtype'] : 'geometry',
    '#disabled' => $has_data,
  );
  
  // If we are running postGIS 2.0, add an option to use geography fields
  try {
    $version = db_query('SELECT substring(PostGIS_Version() from 1 for 3)')->fetchField();
    if (version_compare($version, '2.0', '>=')) {
      $form['fieldtype']['#options']['geography'] = 'Geography';
    }
  }
  catch(PDOException $e) {
    // Do nothing
  }

  $form['srid'] = array(
    '#type' => 'textfield',
    '#title' => t('SRID'),
    '#default_value' => isset($defaults['srid']) ? $defaults['srid'] :  $field['settings']['srid'],
    '#description' => t('Select the map projection that shall be used to map the globe to a plain surface. In general 4326 fits the whole globe pretty well but local projections might give more precise results for specific areas (at the cost of more distortions in other areas).'),
    '#disabled' => $has_data,
    '#required' => TRUE,
  );
  
  return $form;
}


function geofield_postgis_backend_update_field($field, $prior_field, $has_data) {
  if (!$has_data) {
  // from postgis.field.inc in PostGIS module
  $storage = field_sql_storage_field_storage_details($field);

  $connection = Database::getConnection();
  $srid = $field['settings']['postgis']['srid'];
  $type = $field['settings']['postgis']['fieldtype'];

  foreach ($storage['sql'] as $tables) {

    foreach ($tables as $table => $columns) {
      // Get only the geom column
      $column = $columns['geom'];	
      // Use prefix as schema and/or table prefix.
      $prefix = $connection->tablePrefix($table);
      if ($pos = strpos($prefix, '.')) {
        $schema = substr($prefix, 0, $pos);
        $prefix_table = substr($prefix, $pos + 1, strlen($prefix)) . $table;
      } else {
        $schema = '';
        $prefix_table = $prefix . $table;
      }

      db_drop_field($table, $column);

      db_query(
        'SELECT AddGeometryColumn(:schema, :table, :column, :srid, :type, 2)',
        array(
          ':schema' => $schema,
          ':table'  => $prefix_table,
          ':column' => $column,
          ':srid'   => $srid,
          ':type'   => $type,
        )
      );

      db_query('CREATE INDEX ' . $table . '_' . $column . '_idx ON {' . $table . '} USING GIST ( ' . $column . ' )');

      }
    }
  }
}
