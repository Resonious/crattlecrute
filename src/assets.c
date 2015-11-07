#include "assets.h"
#include <stdio.h>
#include <errno.h>
#include <malloc.h>

FILE* assets_file = NULL;

// This should be called just once at the beginning of main()
int open_assets_file() {
    assets_file = fopen("crattlecrute.assets", "rb");
    if (assets_file == NULL) {
        printf("No asset file!!!");
        return errno;
    }
    return 0;
}

AssetFile load_asset(int asset) {
    AssetFile f;
    f.size = ASSETS[asset].size;
    // CLEAN UP AFTER YOURSELF!
    f.bytes = malloc(f.size);
    fseek(assets_file, ASSETS[asset].offset, SEEK_SET);
    fread(f.bytes, 1, f.size, assets_file);

    return f;
}
