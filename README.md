# Gadget Quick Use

OpenRAC mod that allows you to use the Swingshot, Trespasser, and Hydrodisplacer without equipping them first, like in later Ratchet & Clank games.

## How it works

In Ratchet & Clank, Trespasser pads, Hydrodisplacer faucets and Swingshot Versa-Targets are only usable when the respective gadget is already equipped. Later games fixed this inconvenience. This mod does what later games do: if you own the gadget and the pad or the node can be used, you can just use it! For Trespasser pads and Hydrodisplacer faucets, the launcher setting **Use button** chooses which button to use. You'll see a native button prompt when you stand on a pad or faucet, provided you own the gadget and can use it in that situation.

The green glow on a pad or faucet is the game's own. Every other retail condition still applies, so a Hydrodisplacer faucet stays red when the tank is full on a fill-only pad or empty on an drain-only pad. A Swingshot node still has to pass the game's own grapple checks. Already holding the gadget leaves the button alone. Not owning it, and a solved Trespasser pad, stay as they were.

There's also a **Skip Hydrodisplacer camera** setting. Turn it on, and the Hydrodisplacer sequence that pans the camera across the body of water is skipped. The tank still fills or empties as normal.

## Building

Building the mod requires Python 3 and some common build tools: CMake, Ninja, and a compiler (Xcode's command-line tools on macOS, `build-essential` on Linux, Visual Studio with "Desktop development with C++" on Windows).

Once you've installed the required dependencies, clone this repository to get started:
```sh
git clone https://github.com/ParadoxEpoch/rac1-gadget-quick-use.git
cd rac1-gadget-quick-use
```

Then, download the pinned OpenRAC SDK:
```sh
python3 tools/fetch_sdk.py
```

Finally, build and package the mod:
```sh
cmake -S . -B build -G Ninja
cmake --build build --target gadget_quick_use
python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/gadget_quick_use
```

The compiled mod will be in `dist/gadget-quick-use-<version>-<platform>.zip`.

You can then install the packaged mod in the OpenRAC Launcher by dragging the zip file onto the **Add a mod** field in the **Mods** tab.
