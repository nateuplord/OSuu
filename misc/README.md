# misc/

## bad-apple-frames/

Not committed (regenerable, see `.gitignore`). Source video:

https://www.youtube.com/watch?v=FtutLA63Cp8

Extract frames with:

```
ffmpeg -i <video file> -vf "scale=160:120:flags=lanczos" -start_number 0 misc/bad-apple-frames/frame_%05d.png
```
