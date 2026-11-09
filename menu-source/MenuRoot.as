package {
    import flash.display.Sprite;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.text.TextField;
    import flash.text.TextFieldAutoSize;
    import flash.text.TextFormat;

    // OBodyFO4 preset picker.
    // Papyrus (OBodyFO4.psc) calls Setup() after opening the menu, then polls
    // selectedIndex / closeRequested with UI.Get while the menu is open.
    public class MenuRoot extends Sprite {
        // read by Papyrus
        public var selectedIndex:int = -1;
        public var closeRequested:Boolean = false;

        private static const W:Number = 1280;
        private static const H:Number = 720;
        private static const PANEL_W:Number = 380;
        private static const ROW_H:Number = 24;
        private static const VISIBLE:int = 12;
        private static const FONT:String = "$MAIN_Font";
        private static const GREEN:uint = 0x12FF7A;
        private static const DIM:uint = 0x0A3A20;

        private var names:Array = [];
        private var current:int = -1;
        private var cursor:int = 0;
        private var scroll:int = 0;

        private var panel:Sprite = new Sprite();
        private var titleText:TextField;
        private var infoText:TextField;
        private var rows:Array = [];
        private var closeButton:Sprite;

        public function MenuRoot() {
            // compact panel on the right side, no full-screen backdrop,
            // so the actor stays visible
            var panelH:Number = 76 + VISIBLE * ROW_H + 40;
            panel.x = W - PANEL_W - 60;
            panel.y = (H - panelH) / 2;
            panel.graphics.lineStyle(1, GREEN, 0.8);
            panel.graphics.beginFill(0x000000, 0.75);
            panel.graphics.drawRect(0, 0, PANEL_W, panelH);
            panel.graphics.endFill();
            addChild(panel);

            titleText = MakeText(18, GREEN);
            titleText.x = 14; titleText.y = 10; titleText.width = PANEL_W - 28;
            panel.addChild(titleText);

            infoText = MakeText(13, GREEN);
            infoText.x = 14; infoText.y = 40; infoText.width = PANEL_W - 28;
            panel.addChild(infoText);

            for (var i:int = 0; i < VISIBLE; i++) {
                var row:Sprite = new Sprite();
                row.y = 70 + i * ROW_H;
                row.x = 6;
                row.buttonMode = true;
                row.mouseChildren = false;
                var t:TextField = MakeText(15, GREEN);
                t.x = 8; t.y = 1; t.width = PANEL_W - 28;
                t.name = "label";
                row.addChild(t);
                row.addEventListener(MouseEvent.CLICK, OnRowClick);
                row.addEventListener(MouseEvent.MOUSE_OVER, OnRowOver);
                panel.addChild(row);
                rows.push(row);
            }

            closeButton = new Sprite();
            closeButton.buttonMode = true;
            closeButton.mouseChildren = false;
            var ct:TextField = MakeText(13, GREEN);
            ct.text = "Close (Esc)";
            ct.autoSize = TextFieldAutoSize.LEFT;
            closeButton.addChild(ct);
            closeButton.x = PANEL_W - ct.width - 14;
            closeButton.y = panelH - 30;
            closeButton.addEventListener(MouseEvent.CLICK, OnCloseClick);
            panel.addChild(closeButton);

            panel.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
            addEventListener(Event.ADDED_TO_STAGE, OnStage);
            Redraw();
        }

        private function OnStage(e:Event):void {
            stage.addEventListener(KeyboardEvent.KEY_DOWN, OnKey);
            stage.focus = stage;
        }

        private function MakeText(size:int, color:uint):TextField {
            var t:TextField = new TextField();
            var f:TextFormat = new TextFormat(FONT, size, color);
            t.defaultTextFormat = f;
            t.embedFonts = true;
            t.selectable = false;
            t.mouseEnabled = false;
            t.height = size + 10;
            return t;
        }

        // ------------------------------------------------------------ called by Papyrus

        // a_names: preset names separated by "\n"; a_current: index or -1
        public function Setup(a_title:String, a_names:String, a_current:int):void {
            names = a_names.length > 0 ? a_names.split("\n") : [];
            current = a_current;
            cursor = current >= 0 ? current : 0;
            scroll = Math.max(0, Math.min(cursor - int(VISIBLE / 2), names.length - VISIBLE));
            selectedIndex = -1;
            closeRequested = false;
            titleText.text = a_title;
            Redraw();
        }

        public function SetCurrent(a_current:int):void {
            current = a_current;
            Redraw();
        }

        // FO4 menus get controls (Esc, Tab, arrows, gamepad) through this callback
        public function ProcessUserEvent(a_control:String, a_pressed:Boolean):Boolean {
            if (!a_pressed) return false;
            switch (a_control) {
                case "Cancel": case "Pipboy": closeRequested = true; return true;
                case "Up": MoveCursor(-1); return true;
                case "Down": MoveCursor(1); return true;
                case "Accept": Select(cursor); return true;
            }
            return false;
        }

        // ------------------------------------------------------------ input

        private function OnKey(e:KeyboardEvent):void {
            switch (e.keyCode) {
                case 27: case 9: closeRequested = true; break;  // Esc, Tab
                case 38: MoveCursor(-1); break;                  // Up
                case 40: MoveCursor(1); break;                   // Down
                case 13: case 69: Select(cursor); break;         // Enter, E
            }
        }

        private function OnCloseClick(e:MouseEvent):void {
            closeRequested = true;
        }

        private function OnRowClick(e:MouseEvent):void {
            Select(scroll + rows.indexOf(e.currentTarget));
        }

        private function OnRowOver(e:MouseEvent):void {
            var i:int = scroll + rows.indexOf(e.currentTarget);
            if (i < names.length) { cursor = i; Redraw(); }
        }

        private function OnWheel(e:MouseEvent):void {
            scroll = Math.max(0, Math.min(scroll - (e.delta > 0 ? 1 : -1) * 3, names.length - VISIBLE));
            Redraw();
        }

        private function MoveCursor(a_step:int):void {
            if (names.length == 0) return;
            cursor = (cursor + a_step + names.length) % names.length;
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + VISIBLE) scroll = cursor - VISIBLE + 1;
            Redraw();
        }

        private function Select(a_index:int):void {
            if (a_index >= 0 && a_index < names.length) selectedIndex = a_index;
        }

        // ------------------------------------------------------------ drawing

        private function Redraw():void {
            infoText.text = names.length == 0 ? "No presets loaded"
                : "Current: " + (current >= 0 ? names[current] : "unknown") + "    (" + names.length + " presets)";
            for (var i:int = 0; i < VISIBLE; i++) {
                var row:Sprite = rows[i];
                var label:TextField = row.getChildByName("label") as TextField;
                var index:int = scroll + i;
                row.graphics.clear();
                if (index >= names.length) {
                    label.text = "";
                    row.visible = false;
                    continue;
                }
                row.visible = true;
                label.text = (index == current ? "> " : "   ") + names[index];
                var fill:uint = index == cursor ? DIM : 0x000000;
                row.graphics.beginFill(fill, index == cursor ? 1 : 0.01);
                row.graphics.drawRect(0, 0, PANEL_W - 12, ROW_H - 2);
                row.graphics.endFill();
            }
        }
    }
}
