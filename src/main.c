
static void rectangle(qword_t **q, float x1, float y1,
                      float x2, float y2, int r, int g, int b)
{
    PACK_GIFTAG(*q[0], GIF_SET_TAG(4, 1, 0, 0, 0, 1),
                GIF_REG_AD);
    (*q)++;

    PACK_GIFTAG(*q[0], GIF_SET_PRIM(6, 0, 0, 0, 0, 0, 0, 0, 0),
                GIF_REG_PRIM);
    (*q)++;

    PACK_GIFTAG(*q[0], GIF_SET_RGBAQ(r, g, b, 0x80, 0x3f800000),
                GIF_REG_RGBAQ);
    (*q)++;

    PACK_GIFTAG(*q[0], GIF_SET_XYZ(
        ((int)x1 + 2048) << 4,
        ((int)y1 + 2048) << 4, 100), GIF_REG_XYZ2);
    (*q)++;

    PACK_GIFTAG(*q[0], GIF_SET_XYZ(
        ((int)x2 + 2048) << 4,
        ((int)y2 + 2048) << 4, 100), GIF_REG_XYZ2);
    (*q)++;
}
