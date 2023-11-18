# A JPEG steganography algorithm

## Description
There are various methods for hiding additional data inside an image.
The ability to store data inside a lossy format image, such as JPEG,
seems important nowadays, as JPEG is frequently used in social media.

This novel algorithm aims for a non-detectable JPEG steganography,
hopefully erasing the challenges that arise in contrast to a bitmap format.

## Compilation
Build with `cmake`. You need to get a copy of `libjpeg`.

## Usage as standalone
Run `jpeg-steganography <SRC_JPG> <DEST_JPG>`. This implementation is just a
demo, that hides the string `Hello World!` inside the source file and saves
it to the destfile.

## Usage as library
Construct a `jpeg_conceal` object and use the public methods:
- `current_size()` returns the size of the currently store "message".
  Can be used to *estimate* maximum storage size.
- `read()` returns the "message" stored within the image.
  Garbage info may be dangling after the actual message.
- `write(message)` stores a message to the image.

Exceptions may be thrown. Also note, the maximum storage size depends on
the content to store.

## Algorithm concept
All JPEG image *"coefficients"* are iterated over, to write/read the message
bits. The lower coefficients [-2, -1, 0, 1, 2] are left unused, as they are
too sensitive to manipulation (would produce detectable artifacts).
The remaining coefficients store at most a single bit information depending
on whether they are set/read an even or odd value. The algorithm also keeps
track of the bit entropy and will insert/ignore individual bits in order to
leave the total entropy of the image unchanged (this would otherwise be a
strong detection indicator for steganography).

Therefore, the maximum message size is "unknown". One can store about 10%
of the JPEG size.

Do not hesitate to lookup the implementation for details, starting with
method `bit_read()`, which is called upon every coefficient in the JPEG.

## Disclaimer
This algorithm is not yet proven to be safe to use.
