var gm = require('./')
var fs = require('fs')

var convert = gm()
var PngQuant = require('pngquant');
var myPngQuanter = new PngQuant([256, '--ordered']);

fs.createReadStream('in.png')
  .pipe(convert({scale: {width: 1920, height: 1080}, quality: 6}))
  .pipe(fs.createWriteStream('output2.png'))

fs.createReadStream('in.jpg')
  .pipe(convert({scale: 300, rotate: 180, quality: 100}))
  .pipe(fs.createWriteStream('out.jpg'))
