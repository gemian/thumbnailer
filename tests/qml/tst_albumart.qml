Fixture {
    name: "OnlineArt"

    function test_albumart() {
        loadAlbumArt("metallica", "load")
        compare(size.width, 48);
        compare(size.width, 48);
        comparePixel(0, 0, "#C80000");
        comparePixel(47, 0, "#00D200");
        comparePixel(0, 47, "#0000DC");
        comparePixel(47, 47, "#646E78");
    }

    function test_artistart() {
        loadArtistArt("beck", "odelay")
        compare(size.width, 128); // was 640
        compare(size.height, 96); // was 480
        comparePixel(0, 0, "#FE0000");
        comparePixel(127, 0, "#FFFF00");
        comparePixel(0, 95, "#0000FE");
        comparePixel(127, 95, "#00FF01");
    }

    function test_artistart_not_found() {
        loadArtistArt("beck2", "odelay")
        compare(size.width, 96);
        compare(size.height, 96);
    }

    function test_albumart_not_found() {
        loadAlbumArt("beck2", "odelay")
        compare(size.width, 96);
        compare(size.height, 96);
    }

    function test_bad_artist_art_url() {
        loadBadArtistUrl("load")
    }

    function test_bad_album_art_url() {
        loadBadAlbumUrl("metallica")
    }
}
