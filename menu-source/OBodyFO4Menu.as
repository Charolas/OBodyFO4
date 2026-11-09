// Build (Apache Royale mxmlc + playerglobal 11.0), output must NOT be LZMA (ZWS):
//   java -jar mxmlc.jar -load-config=empty-config.xml -external-library-path+=playerglobal.swc
//        -target-player=11.0 -swf-version=13 -compress=false -source-path+=. -output=OBodyFO4Menu.swf OBodyFO4Menu.as
// then zlib-compress to CWS (same format as the vanilla FO4 interface files).
package {
    import flash.display.Sprite;

    // Document class. F4SE opens this as a custom menu with rootPath "root1.Menu_mc",
    // so all the menu logic lives in the Menu_mc child.
    [SWF(width="1280", height="720", backgroundColor="#000000", frameRate="60")]
    public class OBodyFO4Menu extends Sprite {
        public var Menu_mc:MenuRoot;

        public function OBodyFO4Menu() {
            Menu_mc = new MenuRoot();
            Menu_mc.name = "Menu_mc";
            addChild(Menu_mc);
        }
    }
}
