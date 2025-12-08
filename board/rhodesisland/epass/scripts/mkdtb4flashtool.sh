#!/bin/sh


cpp -nostdinc -I "${BUILD_DIR}/linux-5.4.99/include/" \
    -I "${BUILD_DIR}/linux-5.4.99/arch/arm/boot/dts" \
    -P -undef -x assembler-with-cpp -o "${BINARIES_DIR}/devicetree_0.2_flashtool.dts" \
    board/rhodesisland/epass/devicetree/linux/devicetree-0.2.dts

cpp -nostdinc -I "${BUILD_DIR}/linux-5.4.99/include/" \
    -I "${BUILD_DIR}/linux-5.4.99/arch/arm/boot/dts" \
    -P -undef -x assembler-with-cpp -o "${BINARIES_DIR}/devicetree_0.3_flashtool.dts" \
    board/rhodesisland/epass/devicetree/linux/devicetree-0.3.dts
