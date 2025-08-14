# TypeL

`TypeL` is a multiplayer typing application, inspired by [monkeytype](https://monkeytype.com/) and [10fastfingers](https://10fastfingers.com/) designed for linux.

Currently the code is poorly documented and organized, and works only locally (i.e. in the same network where the server is located). I'll do my best to make it work properly `:)`

### Notes

The only dependecy needed for the server is `cJSON`.

The UI is made with `curses`.

In the current implementation, if a player finishes the test he cannot request to change lobby.

### How to run the code?

- run `make` to compile, then you can run the generate executable (currently named `typeL-server`)
- run `<python|python3> UI.py <username>` to connect and play
- when you're done, you can run `make clean`
