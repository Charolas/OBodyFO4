ScriptName OBodyFO4 Native Hidden
{OBodyFO4 preset picker and ORefit.
 OpenPresetMenu is called by the MCM hotkey (keybinds.json).
 UpdateRefit is called by OBodyFO4.dll when a human actor equips/unequips body clothing.}

; ---------------------------------------------------------------- natives (OBodyFO4.dll)

Bool Function CanReceivePreset(ObjectReference akActor) Global Native
Int Function GetTargetActorID() Global Native
String Function GetActorName(ObjectReference akActor) Global Native
Bool Function BeginApply() Global Native
Function EndApply() Global Native
Int Function GetPresetCount(Bool abFemale) Global Native
String Function GetPresetName(Bool abFemale, Int aiPreset) Global Native
String Function GetPresetNamesJoined(Bool abFemale) Global Native
String Function GetMarkerName(Bool abFemale, Int aiPreset) Global Native
Int Function GetSliderCount(Bool abFemale, Int aiPreset) Global Native
String Function GetSliderName(Bool abFemale, Int aiPreset, Int aiSlider) Global Native
Float Function GetSliderValue(Bool abFemale, Int aiPreset, Int aiSlider) Global Native
Int Function PresetIndexFromMorph(Bool abFemale, String asMorph) Global Native
Int Function IsClothed(ObjectReference akActor) Global Native
Function RefitDone(Int aiFormID) Global Native
Function LogRefit(ObjectReference akActor, String asPreset, Bool abClothed) Global Native
Int Function GetRefitDeltaCount(Bool abFemale, Int aiPreset) Global Native
String Function GetRefitDeltaName(Bool abFemale, Int aiPreset, Int aiIndex) Global Native
Float Function GetRefitDeltaValue(Bool abFemale, Int aiPreset, Int aiIndex) Global Native
Bool Function IsRefitDeltaAbsolute(Bool abFemale, Int aiPreset, Int aiIndex) Global Native
String Function DeltaTarget(String asMorph) Global Native
Bool Function IsPlayer(Int aiFormID) Global Native

; ---------------------------------------------------------------- menu

String Function MenuName() Global
    Return "OBodyFO4Menu"
EndFunction

; NPC under the crosshair, or the player when there is none.
Function OpenPresetMenu() Global
    ; one menu at a time
    If !BeginApply()
        Return
    EndIf

    Actor target = Game.GetForm(GetTargetActorID()) as Actor
    If !target
        target = Game.GetPlayer()
    EndIf
    String actorName = GetActorName(target)

    If !CanReceivePreset(target)
        Debug.Notification("OBody: " + actorName + " can't receive presets")
        EndApply()
        Return
    EndIf

    Bool female = target.GetLeveledActorBase().GetSex() == 1
    If GetPresetCount(female) <= 0
        Debug.Notification("OBody: no presets loaded")
        EndApply()
        Return
    EndIf

    String menu = MenuName()
    If !UI.IsMenuRegistered(menu)
        ; Interface\OBodyFO4Menu.swf, logic lives in stage.Menu_mc
        UI.RegisterBasicCustomMenu(menu, "OBodyFO4Menu", "root1.Menu_mc")
    EndIf
    UI.OpenMenu(menu)

    Int tries = 0
    While !UI.IsMenuOpen(menu) && tries < 60
        Utility.WaitMenuMode(0.05)
        tries += 1
    EndWhile
    If !UI.IsMenuOpen(menu)
        Debug.Notification("OBody: could not open the preset menu")
        EndApply()
        Return
    EndIf

    Var[] setup = new Var[3]
    setup[0] = "OBody - " + actorName
    setup[1] = GetPresetNamesJoined(female)
    setup[2] = GetCurrentPresetIndex(target, female)
    UI.Invoke(menu, "root.Menu_mc.Setup", setup)

    ; polls only while the menu is open; picking a preset applies it and closes the menu
    Int selected = -1
    While UI.IsMenuOpen(menu) && selected < 0
        If UI.Get(menu, "root.Menu_mc.closeRequested") as Bool
            UI.CloseMenu(menu)
        Else
            selected = UI.Get(menu, "root.Menu_mc.selectedIndex") as Int
            If selected < 0
                Utility.WaitMenuMode(0.05)
            EndIf
        EndIf
    EndWhile

    If selected >= 0
        UI.CloseMenu(menu)
        ApplyPreset(target, female, selected)
        Debug.Notification("OBody: " + actorName + " -> " + GetPresetName(female, selected))
    EndIf
    EndApply()
EndFunction

; ---------------------------------------------------------------- presets

; Index of the preset this actor has, from the marker morph. -1 = unknown.
; LooksMenu keeps a removed morph's name in GetMorphs with no value left,
; so only a marker that still has a value counts.
Int Function GetCurrentPresetIndex(Actor akActor, Bool abFemale) Global
    String[] morphs = BodyGen.GetMorphs(akActor, abFemale)
    Int i = 0
    While i < morphs.Length
        Int index = PresetIndexFromMorph(abFemale, morphs[i])
        If index >= 0 && BodyGen.GetMorph(akActor, abFemale, morphs[i], None) > 0.0
            Return index
        EndIf
        i += 1
    EndWhile
    Return -1
EndFunction

; Applies a preset (menu choice), plus ORefit on top if the actor is dressed.
; Replaces the BodyGen morphs (keyword None) and keeps keyword-tagged morphs from other mods.
Function ApplyPreset(Actor akActor, Bool abFemale, Int aiPreset) Global
    BodyGen.RemoveMorphsByKeyword(akActor, abFemale, None)
    Int sliderCount = GetSliderCount(abFemale, aiPreset)
    Int i = 0
    While i < sliderCount
        BodyGen.SetMorph(akActor, abFemale, GetSliderName(abFemale, aiPreset, i), None, GetSliderValue(abFemale, aiPreset, i))
        i += 1
    EndWhile
    BodyGen.SetMorph(akActor, abFemale, GetMarkerName(abFemale, aiPreset), None, 1.0)
    If WantsRefit(akActor) && IsClothed(akActor) == 1
        ApplyRefit(akActor, abFemale, aiPreset)
    EndIf
    BodyGen.UpdateMorphs(akActor)
EndFunction

; ---------------------------------------------------------------- ORefit
; ORefit never replaces the body: it changes only its own sliders, on top of whatever
; the actor has (preset or a hand-made LooksMenu body), and remembers each change in
; an "OBodyFO4Delta_<slider>" morph so RemoveRefit undoes exactly that.

Bool Function IsRefitEnabled() Global
    Return MCM.GetModSettingBool("OBodyFO4", "bORefit:General")
EndFunction

Bool Function WantsRefit(Actor akActor) Global
    If !IsRefitEnabled()
        Return False
    EndIf
    If akActor == Game.GetPlayer()
        Return MCM.GetModSettingBool("OBodyFO4", "bORefitPlayer:General")
    EndIf
    Return True
EndFunction

; aiPreset -1 = generic ORefit (no preset of ours on this actor)
Function ApplyRefit(Actor akActor, Bool abFemale, Int aiPreset) Global
    If BodyGen.GetMorph(akActor, abFemale, "OBodyFO4Refit", None) > 0.0
        Return
    EndIf
    Int count = GetRefitDeltaCount(abFemale, aiPreset)
    Int i = 0
    While i < count
        String slider = GetRefitDeltaName(abFemale, aiPreset, i)
        Float current = BodyGen.GetMorph(akActor, abFemale, slider, None)
        Float target = current + GetRefitDeltaValue(abFemale, aiPreset, i)
        If IsRefitDeltaAbsolute(abFemale, aiPreset, i)
            target = GetRefitDeltaValue(abFemale, aiPreset, i)
        EndIf
        Float change = target - current
        If change != 0.0
            BodyGen.SetMorph(akActor, abFemale, slider, None, target)
            BodyGen.SetMorph(akActor, abFemale, "OBodyFO4Delta_" + slider, None, change)
        EndIf
        i += 1
    EndWhile
    BodyGen.SetMorph(akActor, abFemale, "OBodyFO4Refit", None, 1.0)
EndFunction

; Undoes exactly what ApplyRefit changed. Returns False if there was nothing to undo.
Bool Function RemoveRefit(Actor akActor, Bool abFemale) Global
    Bool undone = False
    String[] morphs = BodyGen.GetMorphs(akActor, abFemale)
    Int i = 0
    While i < morphs.Length
        String slider = DeltaTarget(morphs[i])
        If slider != ""
            Float change = BodyGen.GetMorph(akActor, abFemale, morphs[i], None)
            If change != 0.0
                Float current = BodyGen.GetMorph(akActor, abFemale, slider, None)
                BodyGen.SetMorph(akActor, abFemale, slider, None, current - change)
                undone = True
            EndIf
            BodyGen.RemoveMorphsByName(akActor, abFemale, morphs[i])
        EndIf
        i += 1
    EndWhile
    BodyGen.RemoveMorphsByName(akActor, abFemale, "OBodyFO4Refit")
    Return undone
EndFunction

; Called by OBodyFO4.dll (TESEquipEvent) when body clothing changes on a human actor.
Function UpdateRefit(Int aiFormID) Global
    Actor akActor = Game.GetForm(aiFormID) as Actor
    ; let the game finish the equip (the worn-item 3D updates a few frames later)
    Utility.Wait(0.1)
    If akActor && akActor.Is3DLoaded()
        Bool female = akActor.GetLeveledActorBase().GetSex() == 1
        Int preset = GetCurrentPresetIndex(akActor, female)
        If preset < 0 && !IsPlayer(aiFormID)
            ; freshly loaded NPC: give BodyGen a moment to assign its preset
            Utility.Wait(1.0)
            preset = GetCurrentPresetIndex(akActor, female)
        EndIf
        Int clothed = IsClothed(akActor)
        ; NPCs need one of our presets; the player can have any (hand-made) body
        If clothed >= 0 && (preset >= 0 || IsPlayer(aiFormID))
            Bool wantRefit = clothed == 1 && WantsRefit(akActor)
            Bool hasRefit = BodyGen.GetMorph(akActor, female, "OBodyFO4Refit", None) > 0.0
            String presetName = "custom body"
            If preset >= 0
                presetName = GetPresetName(female, preset)
            EndIf
            If wantRefit && !hasRefit
                ApplyRefit(akActor, female, preset)
                BodyGen.UpdateMorphs(akActor)
                LogRefit(akActor, presetName, True)
            ElseIf !wantRefit && hasRefit
                If !RemoveRefit(akActor, female) && preset >= 0
                    ; refit from older versions (no deltas stored): reapply the nude preset
                    ApplyPreset(akActor, female, preset)
                EndIf
                BodyGen.UpdateMorphs(akActor)
                LogRefit(akActor, presetName, False)
            EndIf
        EndIf
    EndIf
    RefitDone(aiFormID)
EndFunction
