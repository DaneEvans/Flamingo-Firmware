Flamingo exists to modify Meshtastic firmware for use in caves, and in particular cave rescue.

## Flashing

At the moment we have no automated process.

Check out the Flamingo branch or tag that you wish to use, and then use platform IO in VSCode to build and upload the firmware.

## Repository

This repository is a clone of the base Meshtastic repository. It is intentionally not a clone in order to better maintain separation of Flamingo and Meshtastic concerns, and make it easier to maintain.

### Branches

- Main: Houses the readmes, and github CI actions.
- meshtastic_master: Shadows the Meshtastic/firmware:master branch - kept in sync by CI.
- meshtastic_develop: Shadows the Meshtastic/firmware:develop branch - kept in sync by CI.
- flamingo: This is the branch that contains all of the flamingo changes. This is what changes to the flamingo firmware should be based off, and merged back to.
- flamingo_updated: This applies the flamingo patch to meshtastic_master
- flamingo_updated_dev: This applies the flamingo patch to meshtastic_develop

### Tags

Meshtastic has tags.

I think what we are going to have to do is have flamingo tags as needed, and maintain a compatability table.

Ideally the flamingo code is portable, and could be cherry-picked back onto an earlier version if needed, but as that's not typically going to be necessary, and is unlikely to happen all that much, the main purpose is to make it easy to work on, and easier to see the changes between the current versions, and what existed in the past, or roll back to an older configuration.

To that end, we maintain flamingo tags as well.
These should be able to be merged onto later meshtastic versions with minimal impact (excluding conflicts.)

| Flamingo version         | Meshtastic base | Comments                                          |
| ------------------------ | --------------- | ------------------------------------------------- |
| flamingo_2511            | v2.7.9          | Starting to use this Flamingo-firmware repo       |
| Flamingo_fw2_7_generated | v2.7.9          | Huntsville - added FLAMINGO define                |
| flamingo_2510            | v2.7.10         | Aussie, pre #defines                              |
| jun2025                  | v2.5.20         | Huntsville - buzzer pin is set for wismesh pocket |
