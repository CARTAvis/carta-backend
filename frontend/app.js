namespace = '/test';
var fileLoaded = false;
var regionRequestTime = 0;
var regionReturnTime = 0;


var gl;
var extTextureFloat;

function initGL(canvasGL) {
    try {
        gl = canvasGL.getContext("webgl");
        if (!gl) {
            return;
        }
        gl.viewportWidth = canvasGL.width;
        gl.viewportHeight = canvasGL.height;
    } catch (e) {
        console.log(e);
    }
    if (!gl) {
        alert("Could not initialise WebGL");
    }

    extTextureFloat = gl.getExtension('OES_texture_float');
    if (!extTextureFloat) {
        alert("Could not initialise WebGL extension");
    }
}

function getShader(gl, id) {
    var shaderScript = document.getElementById(id);
    if (!shaderScript) {
        return null;
    }

    var str = "";
    var k = shaderScript.firstChild;
    while (k) {
        if (k.nodeType == 3) {
            str += k.textContent;
        }
        k = k.nextSibling;
    }

    var shader;
    if (shaderScript.type == "x-shader/x-fragment") {
        shader = gl.createShader(gl.FRAGMENT_SHADER);
    } else if (shaderScript.type == "x-shader/x-vertex") {
        shader = gl.createShader(gl.VERTEX_SHADER);
    } else {
        return null;
    }

    gl.shaderSource(shader, str);
    gl.compileShader(shader);

    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        alert(gl.getShaderInfoLog(shader));
        return null;
    }

    return shader;
}


var shaderProgram;

function initShaders() {
    var fragmentShader = getShader(gl, extTextureFloat ? "cmap-float" : "cmap-rgba");
    var vertexShader = getShader(gl, "vs");

    shaderProgram = gl.createProgram();
    gl.attachShader(shaderProgram, vertexShader);
    gl.attachShader(shaderProgram, fragmentShader);
    gl.linkProgram(shaderProgram);

    if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {
        alert("Could not initialise shaders");
    }

    gl.useProgram(shaderProgram);

    shaderProgram.vertexPositionAttribute = gl.getAttribLocation(shaderProgram, "aVertexPosition");
    gl.enableVertexAttribArray(shaderProgram.vertexPositionAttribute);

    shaderProgram.vertexUVAttribute = gl.getAttribLocation(shaderProgram, "aVertexUV");
    gl.enableVertexAttribArray(shaderProgram.vertexUVAttribute);

    shaderProgram.pMatrixUniform = gl.getUniformLocation(shaderProgram, "uPMatrix");
    shaderProgram.mvMatrixUniform = gl.getUniformLocation(shaderProgram, "uMVMatrix");

    shaderProgram.MinValUniform = gl.getUniformLocation(shaderProgram, "uMinVal");
    shaderProgram.MaxValUniform = gl.getUniformLocation(shaderProgram, "uMaxVal");
    shaderProgram.MinColorUniform = gl.getUniformLocation(shaderProgram, "uMinCol");
    shaderProgram.MaxColorUniform = gl.getUniformLocation(shaderProgram, "uMaxCol");

    shaderProgram.ViewportSizeUniform = gl.getUniformLocation(shaderProgram, "uViewportSize");
}


function setUniforms() {
    gl.uniform2f(
        shaderProgram.ViewportSizeUniform,
        gl.viewportWidth,
        gl.viewportHeight
    );
}


var squareVertexPositionBuffer;
var squareVertexUVBuffer;

function initBuffers() {
    //Create Square Position Buffer
    squareVertexPositionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexPositionBuffer);
    var vertices = [
        1.0, 1.0, 0.0,
        -1.0, 1.0, 0.0,
        1.0, -1.0, 0.0,
        -1.0, -1.0, 0.0
    ];
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
    squareVertexPositionBuffer.itemSize = 3;
    squareVertexPositionBuffer.numItems = 4;

    //Create Square UV Buffer
    squareVertexUVBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexUVBuffer);
    var uvs = [
        1.0, 1.0,
        0.0, 1.0,
        1.0, 0.0,
        0.0, 0.0
    ];

    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(uvs), gl.STATIC_DRAW);
    squareVertexUVBuffer.itemSize = 2;
    squareVertexUVBuffer.numItems = 4;
}


function drawScene() {
    gl.viewport(0, 0, gl.viewportWidth, gl.viewportHeight);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexPositionBuffer);
    gl.vertexAttribPointer(shaderProgram.vertexPositionAttribute, squareVertexPositionBuffer.itemSize, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, squareVertexUVBuffer);
    gl.vertexAttribPointer(shaderProgram.vertexUVAttribute, squareVertexUVBuffer.itemSize, gl.FLOAT, false, 0, 0);
    gl.uniform2f(shaderProgram.ViewportSizeUniform, gl.viewportWidth, gl.viewportHeight);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, squareVertexPositionBuffer.numItems);
}

function loadFP32Texture(data, width, height) {
    // Create a texture.
    var texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);

    // fill texture with 3x2 pixels
    const level = 0;
    const internalFormat = gl.LUMINANCE;
    const border = 0;
    const format = gl.LUMINANCE;
    const type = gl.FLOAT;

    const alignment = 1;
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
    gl.texImage2D(gl.TEXTURE_2D, level, internalFormat, width, height, border,
        format, type, data);

    // set the filtering so we don't need mips and it's not filtered
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function loadRGBATexture(data, width, height) {
    // Create a texture.
    var texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);

    const level = 0;
    const internalFormat = gl.RGBA;
    const border = 0;
    const format = gl.RGBA;
    const type = gl.UNSIGNED_BYTE;

    const alignment = 1;
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
    gl.texImage2D(gl.TEXTURE_2D, level, internalFormat, width, height, border,
        format, type, data);

    // set the filtering so we don't need mips and it's not filtered
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}


$(document).ready(function () {
    connection = new WebSocket('ws://10.0.0.3:3002');
    connection.binaryType = 'arraybuffer';

    var canvasGL = document.getElementById("webgl");
    initGL(canvasGL);
    initShaders();
    initBuffers();

    gl.clearColor(0.0, 0.0, 0.0, 1.0);
    gl.enable(gl.DEPTH_TEST);

    var minVal = 2.5;
    var maxVal = 6.5;
    var regionImageData = null;


    $("#min_val").on("input", $.debounce(1, function () {
        minVal = this.value;
        refreshColorScheme();
    }));

    $("#max_val").on("input", $.debounce(1, function () {
        maxVal = this.value;
        refreshColorScheme();
    }));

    var max_col = hexToRGB(document.getElementById("max_col").value);
    $("#max_col").on("input", $.debounce(1, function () {
        max_col = hexToRGB(this.value);
        refreshColorScheme();
    }));

    var min_col = hexToRGB(document.getElementById("min_col").value);
    $("#min_col").on("input", $.debounce(1, function () {
        min_col = hexToRGB(this.value);
        refreshColorScheme();
    }));

    $("#webgl").on("mousemove", $.debounce(16.6, function (evt) {
        if (!regionImageData)
            return;
        var mousePos = getMousePos(canvasGL, evt);
        var dataPos = {x: Math.floor(mousePos.x / regionImageData.mip), y: Math.floor(mousePos.y / regionImageData.mip)};
        var zVal = regionImageData.fp32payload[dataPos.y * regionImageData.w + dataPos.x];
        //var cursorInfo = `(${dataPos.x * regionImageData.mip + regionImageData.x}, ${dataPos.y * regionImageData.mip + regionImageData.y}): ${zVal.toFixed(3)}`;
        var cursorInfo = '(' + dataPos.x * regionImageData.mip + regionImageData.x + ', ' + dataPos.y * regionImageData.mip + regionImageData.y + '): ' + zVal.toFixed(3);
        $("#cursor").html(cursorInfo);
    }))
    ;

    connection.onopen = function () {
        console.log("Connected");
    };

    // Log errors
    connection.onerror = function (error) {
        console.log('WebSocket Error ' + error);
    };

    // Log messages from the server
    connection.onmessage = function (event) {

        var binaryPayload = null;
        var jsonPayload = null;

        if (event.data instanceof ArrayBuffer) {
            var binaryLength = new DataView(event.data.slice(0, 4)).getUint32(0, true);
            binaryPayload = new Float32Array(event.data.slice(4, 4 + binaryLength));
            jsonPayload = String.fromCharCode.apply(null, new Uint8Array(event.data, 4 + binaryLength,));
        }
        else
            jsonPayload = event.data;

        var eventData = JSON.parse(jsonPayload);
        var eventName = eventData.event;
        var message = eventData.message;


        if (eventName === 'region_read' && message.success) {
            regionImageData = message;

            //console.log(regionImageData);
            if (regionImageData.compressed >= 4) {
                //console.time("decompress");
                var compressedPayload = new Uint8Array(binaryPayload.buffer);
                regionImageData.fp32payload = zfpDecompressUint8WASM(compressedPayload, regionImageData.w, regionImageData.h, regionImageData.compressed);
                //console.timeEnd("decompress");
            }
            else
                regionImageData.fp32payload = binaryPayload;

            if (extTextureFloat) {
                loadFP32Texture(regionImageData.fp32payload, regionImageData.w, regionImageData.h);
            }
            else {
                regionImageData.u8payload = encodeToUint8WASM(binaryPayload);
                loadRGBATexture(regionImageData.u8payload, regionImageData.w, regionImageData.h);
            }

            refreshColorScheme();
        }
        console.timeEnd("region_rtt");
        console.log('Server event: ' + eventName);
    };

    function refreshColorScheme() {
        if (!regionImageData)
            return;
        gl.uniform1f(shaderProgram.MinValUniform, minVal);
        gl.uniform1f(shaderProgram.MaxValUniform, maxVal);
        gl.uniform4f(shaderProgram.MinColorUniform, min_col.r / 255.0, min_col.g / 255.0, min_col.b / 255.0, 1.0);
        gl.uniform4f(shaderProgram.MaxColorUniform, max_col.r / 255.0, max_col.g / 255.0, max_col.b / 255.0, 1.0);
        gl.uniform2f(shaderProgram.ViewportSizeUniform, gl.viewportWidth, gl.viewportHeight);
        drawScene();
    }

    function encodeToUint8WASM(f) {
        //var uint8Data = new Uint8Array(f.length * 4);

        encodeFloats = Module.cwrap(
            'encodeFloats', 'number', ['number', 'number', 'number']
        );
        var nDataBytes = f.length * f.BYTES_PER_ELEMENT;
        var dataPtr = Module._malloc(nDataBytes);
        var dataPtrUint = Module._malloc(nDataBytes);
        var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
        dataHeap.set(new Uint8Array(f.buffer));
        var dataHeapUint = new Uint8Array(Module.HEAPU8.buffer, dataPtrUint, nDataBytes);
        dataHeapUint.set(new Uint8Array(dataPtrUint.buffer));

        // Call function and get result
        encodeFloats(dataHeap.byteOffset, dataHeapUint.byteOffset, f.length);
        var resultUint = new Uint8Array(dataHeapUint.buffer, dataHeapUint.byteOffset, f.length * 4);
        // Free memory
        Module._free(dataHeap.byteOffset);
        Module._free(dataHeapUint.byteOffset);

        return resultUint;
        // END WASM

    }

    function zfpDecompressUint8WASM(u8, nx, ny, precision) {
        var f = new Float32Array(nx * ny);

        zfpDecompress = Module.cwrap(
            'zfpDecompress', 'number', ['number', 'number', 'number', 'number', 'number', 'number']
        );
        var nDataBytes = f.length * f.BYTES_PER_ELEMENT;
        var dataPtr = Module._malloc(nDataBytes);
        var nDataBytesCompressed = u8.length;
        var dataPtrUint = Module._malloc(nDataBytesCompressed);
        var dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
        dataHeap.set(new Uint8Array(f.buffer));
        var dataHeapUint = new Uint8Array(Module.HEAPU8.buffer, dataPtrUint, nDataBytesCompressed);
        dataHeapUint.set(new Uint8Array(u8.buffer));

        // Call function and get result
        zfpDecompress(parseInt(precision), dataHeap.byteOffset, nx, ny, dataHeapUint.byteOffset, u8.length);
        var resultFloat = new Float32Array(dataHeap.buffer, dataHeap.byteOffset, f.length);
        // Free memory
        Module._free(dataHeap.byteOffset);
        Module._free(dataHeapUint.byteOffset);

        return resultFloat;
        // END WASM

    }

    function encodeToUint8(f) {
        var u8 = new Uint8Array(4 * f.length);
        var dv = new DataView(f.buffer);

        for (var i = 0, offset = 0; i < f.length; i++, offset += 4) {
            var xi = dv.getUint32(offset, true);
            var v = -1;
            v = (xi >> 31 & 1) | ((xi & 0x7fffff) << 1);
            u8[offset] = v / 0x10000;
            u8[offset + 1] = (v % 0x10000) / 0x100;
            u8[offset + 2] = v % 0x100;
            u8[offset + 3] = xi >> 23 & 0xff;
        }
        return u8;
    }

    function hexToRGB(hex) {
        hex = hex.toUpperCase();
        var h = "0123456789ABCDEF";
        var r = h.indexOf(hex[1]) * 16 + h.indexOf(hex[2]);
        var g = h.indexOf(hex[3]) * 16 + h.indexOf(hex[4]);
        var b = h.indexOf(hex[5]) * 16 + h.indexOf(hex[6]);
        return {r, g, b};
    }

    function getMousePos(canvas, evt) {
        var rect = canvas.getBoundingClientRect();
        return {
            x: evt.clientX - rect.left,
            y: evt.clientY - rect.top
        };
    }

    $('form#region').submit(function (event) {
        if (connection) {
            console.time("region_rtt");
            var payload = {
                event: "region_read",
                message: {
                    band: parseInt($('#band_val').val()),
                    x: parseInt($('#x_val').val()),
                    y: parseInt($('#y_val').val()),
                    w: parseInt($('#w_val').val()),
                    h: parseInt($('#h_val').val()),
                    mip: parseInt($('#mip_val').val()),
                    compression: parseInt($('#compression_val').val())
                }
            };
            connection.send(JSON.stringify(payload));
        }
        return false;
    });

    $('form#fileload').submit(function (event) {
        if (connection) {
            var payload = {
                event: "fileload",
                message: {
                    filename: $('#fileload_data').val()
                }
            };

            console.log(payload);
            connection.send(JSON.stringify(payload));
        }
        return false;
    });

});