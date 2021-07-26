# Convert ichat Files
If you have .ichat logs floating around your hard drive and wish that you didn't have to open them with iChat/Messages in order to read them, then this program is for you; it will convert .ichat files to either RTF or plain-text, depending on your preference.

I have included a prebuilt binary in the Build folder — it was built for macOS, but the source code is platform-agnostic as far as I know, being written in C, so you should be able to build CiF for Windows or Linux.

## Downloading
Regular users can simply choose to download this project as a ZIP using the "Code" button.

## Running
If you use the prebuilt version of the app in Build/ directly, it must be invoked from the command line as `"./Build/Convert ichat Files"`. For documentation, simply run the program without any arguments. CiF only converts a single .ichat file at a time.

To run the program on a directory full of .ichat files, use the Bash script "batch_convert_ichat_files.sh" from your command line:
```
./batch_convert_ichat_files.sh folder_with_ichat_files
```

## Notes
- This program was developed only as far as was needed to convert my set of test files (about 600 logs). It's likely that there are various quirks in .ichat files out there in the wild that this program does not account for; feel free to report a bug if you find one.
- This program is not fully Unicode-friendly, so names in a non-English alphabet may not be supported without a little additional work.

![Preview](https://github.com/Amethyst-Software/convert-ichat-files/blob/main/preview.png)

![RTF sample](https://github.com/Amethyst-Software/convert-ichat-files/blob/main/sample-rtf-output.png)

![TXT sample](https://github.com/Amethyst-Software/convert-ichat-files/blob/main/sample-txt-output.png)
