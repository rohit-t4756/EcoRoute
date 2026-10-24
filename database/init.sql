-- A. Enable PostGIS's spatial features
create extension if not exists postgis;

-- B. Create custom data types
create type road_cat as ENUM('express_and_highways', 'district_roads', 'rural_roads', 'arterial_roads', 'alleyways');
create type road_t as  ENUM('paved', 'unpaved', 'dirt');

-- C. Schema
create table nodes (
    -- identification
    id bigint primary key,
    name varchar(100),
    -- attributes
    elevation float,
    location geometry(Point, 4326) not null    -- NOTE: postgis expects (longitude, latitude)
);

create table edges (
    -- identification
    id bigint primary key,
    -- start and end node IDs (Foreign Keys)
    source_id bigint,
    foreign key (source_id) references nodes(id),
    target_id bigint,
    foreign key (target_id) references nodes(id),
    -- attributes
    distance float not null,
    speed_limit int,
    -- categorisation
    road_category  road_cat default 'district_roads',
    road_type road_t default 'unpaved',
    geometry geometry(LineString, 4326)
);