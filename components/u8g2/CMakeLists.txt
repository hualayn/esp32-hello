# u8g2 组件（纯 C 库，来自 olikraus/u8g2 主仓库 csrc/）
# 字体从官方 u8g2_fonts.c 中抽取：wqy12 中文字库 + 两个 ASCII 字库
file(GLOB U8G2_SRCS "src/*.c")
idf_component_register(SRCS ${U8G2_SRCS} "fonts/fonts.c"
                       INCLUDE_DIRS "src" "fonts")
