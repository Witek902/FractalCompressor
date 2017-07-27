#pragma once

// number of bits per domain location (one dimension)
// for 512x512 images, 8 bits should be optimal
#define DOMAIN_LOCATION_BITS        6
#define DOMAIN_MAX_TRANSFORMS       (1 << (DOMAIN_LOCATION_BITS - 1))

#define DOMAIN_TRANSFORM_BITS       3

#define DOMAIN_SCALE_BITS           7
#define DOMAIN_SCALE_RANGE_BITS     1
#define DOMAIN_SCALE_RANGE (1 << (DOMAIN_SCALE_RANGE_BITS - 1))

#define DOMAIN_OFFSET_BITS          7
#define DOMAIN_OFFSET_RANGE_BITS    9
#define DOMAIN_OFFSET_RANGE (1 << (DOMAIN_OFFSET_RANGE_BITS - 1))