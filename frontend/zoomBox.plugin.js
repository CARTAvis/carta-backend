function getScaledValues(chart, event) {
    var x = 0;
    var y = 0;

    for (var scaleName in chart.scales) {
        var scale = chart.scales[scaleName];
        if (scale.isHorizontal()) {
            x = scale.getValueForPixel(event.x);
        }
        else {
            y = scale.getValueForPixel(event.y);
        }
    }
    return {x, y};
}

Chart.plugins.register({
    beforeInit: function (chart) {
        // Merge zoomBox options
        var defaultOptions = {
            doubleClickDelay: 300,
            deltaLimit: 20,
            mode: 'xy',
            rectangleSelect: true,
            rescale: false,
            fillStyle: "#e0d7d955",
            lineWidth: 1,
            strokeStyle: "grey",
            showOverlay: true,
            strokeStyleOverlay: "darkgrey",
            lineWidthOverlay: 3
        };
        chart.zoomBox = {
            options: Chart.helpers.configMerge(defaultOptions, chart.options.zoomBox)
        };

        // Sanity checks for zoom modes
        if (chart.zoomBox.options.mode !== 'xy')
            chart.zoomBox.options.rectangleSelect = false;

    },
    afterEvent: function (chart, event) {
        event.native.preventDefault();
        var options = chart.zoomBox.options;

        if (!chart.zoomBox)
            chart.zoomBox = {};
        if (event.type === "mousedown") {
            chart.zoomBox.pixelInitial = {x: event.x, y: event.y};
            chart.zoomBox.scaleInitial = getScaledValues(chart, event);
            chart.zoomBox.dragging = true;
            chart.zoomBox.pixelFinal = null;
            chart.zoomBox.scaleFinal = null;

        }
        else if (event.type === "mouseup") {

            var tClick = Date.now();
            if (chart.zoomBox.prevClick > 0 && (tClick - chart.zoomBox.prevClick) < options.doubleClickDelay) {
                chart.zoomBox.prevClick = -1;

                if (chart.zoomBox.originalZoom) {
                    chart.options.scales.xAxes[0].ticks.min = chart.zoomBox.originalZoom.xMin;
                    chart.options.scales.xAxes[0].ticks.max = chart.zoomBox.originalZoom.xMax;
                    chart.options.scales.yAxes[0].ticks.min = chart.zoomBox.originalZoom.yMin;
                    chart.options.scales.yAxes[0].ticks.max = chart.zoomBox.originalZoom.yMax;
                    chart.update({duration: 0});
                }
                chart.zoomBox.dragging = false;
                chart.zoomBox.pixelCurrent = null;
                chart.zoomBox.scaleCurrent = null;
                chart.zoomBox.pixelFinal = null;
                chart.zoomBox.scaleFinal = null;
                chart.zoomBox.pixelInitial = null;
                chart.zoomBox.scaleInitial = null;
            }
            else {
                chart.zoomBox.prevClick = tClick;
                chart.zoomBox.pixelFinal = {x: event.x, y: event.y};
                chart.zoomBox.scaleFinal = getScaledValues(chart, event);
                chart.zoomBox.dragging = false;

                if (chart.zoomBox.pixelCurrent) {
                    chart.zoomBox.pixelCurrent = null;
                    chart.zoomBox.scaleCurrent = null;


                    if (!chart.zoomBox.originalZoom) {
                        chart.zoomBox.originalZoom = {
                            xMin: chart.options.scales.xAxes[0].ticks.min,
                            xMax: chart.options.scales.xAxes[0].ticks.max,
                            yMin: chart.options.scales.yAxes[0].ticks.min,
                            yMax: chart.options.scales.yAxes[0].ticks.max
                        };
                    }

                    if (chart.zoomBox.zoomMode == 'xy') {
                        chart.options.scales.xAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.xAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.yAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.options.scales.yAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.update({duration: 0});
                    }
                    else if (chart.zoomBox.zoomMode == 'x') {
                        chart.options.scales.xAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.options.scales.xAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.x, chart.zoomBox.scaleFinal.x);
                        chart.update({duration: 0});
                    }
                    else if (chart.zoomBox.zoomMode == 'y') {
                        chart.options.scales.yAxes[0].ticks.min = Math.min(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.options.scales.yAxes[0].ticks.max = Math.max(chart.zoomBox.scaleInitial.y, chart.zoomBox.scaleFinal.y);
                        chart.update({duration: 0});
                    }
                }
            }
        }
        else if (event.type === "mousemove" && chart.zoomBox && chart.zoomBox.dragging) {
            chart.zoomBox.pixelCurrent = {x: event.x, y: event.y};
            chart.zoomBox.scaleCurrent = getScaledValues(chart, event);
        }
    },
    beforeDraw: function (chart, easing) {
        var ctx = chart.chart.ctx;
        var options = chart.zoomBox.options;
        if (chart.zoomBox && chart.zoomBox.dragging && chart.zoomBox.pixelInitial && chart.zoomBox.pixelCurrent) {

            var dX = chart.zoomBox.pixelCurrent.x - chart.zoomBox.pixelInitial.x;
            var dY = chart.zoomBox.pixelCurrent.y - chart.zoomBox.pixelInitial.y;

            // Rectangle mode
            if (options.rectangleSelect && Math.abs(dX) > options.deltaLimit && Math.abs(dY) > options.deltaLimit) {
                chart.zoomBox.zoomMode = 'xy';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.stroke();
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                if (options.showOverlay) {
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y + (dY>0?1:-1)*options.deltaLimit/2);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + (dX>0?1:-1)*options.deltaLimit/2, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelCurrent.y - (dY>0?1:-1)*options.deltaLimit/2);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + (dX>0?1:-1)*options.deltaLimit/2, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y + (dY>0?1:-1)*options.deltaLimit/2);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x - (dX>0?1:-1)*options.deltaLimit/2, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelCurrent.y - (dY>0?1:-1)*options.deltaLimit/2);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x - (dX>0?1:-1)*options.deltaLimit/2, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                }
                ctx.restore();
            }
            // H-Zoom only (vertical lines)
            else if ((options.mode === 'xy' && Math.abs(dX) > Math.abs(dY)) || options.mode === 'x') {
                chart.zoomBox.zoomMode = 'x';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(chart.zoomBox.pixelInitial.x, 0, dX, 1e6);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(chart.zoomBox.pixelInitial.x, 0, dX, 1e6);
                ctx.stroke();
                if (options.showOverlay) {
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y - options.deltaLimit);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y + options.deltaLimit);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y - options.deltaLimit);
                    ctx.lineTo(chart.zoomBox.pixelCurrent.x, chart.zoomBox.pixelInitial.y + options.deltaLimit);
                    ctx.stroke();
                }
                ctx.restore();
            }
            // V-Zoom only (horizontal lines)
            else if (options.mode === 'xy' || options.mode === 'y'){
                chart.zoomBox.zoomMode = 'y';
                ctx.save();
                ctx.fillStyle = options.fillStyle;
                ctx.fillRect(0, chart.zoomBox.pixelInitial.y, 1e6, dY);
                ctx.lineWidth = options.lineWidth;
                ctx.strokeStyle = options.strokeStyle;
                ctx.rect(0, chart.zoomBox.pixelInitial.y, 1e6, dY);
                ctx.stroke();
                if (options.showOverlay) {
                    ctx.lineWidth = options.lineWidthOverlay;
                    ctx.strokeStyle = options.strokeStyleOverlay;
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x - options.deltaLimit, chart.zoomBox.pixelInitial.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + options.deltaLimit, chart.zoomBox.pixelInitial.y);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(chart.zoomBox.pixelInitial.x - options.deltaLimit, chart.zoomBox.pixelCurrent.y);
                    ctx.lineTo(chart.zoomBox.pixelInitial.x + options.deltaLimit, chart.zoomBox.pixelCurrent.y);
                    ctx.stroke();
                }
                ctx.restore();
            }
        }
    }
});