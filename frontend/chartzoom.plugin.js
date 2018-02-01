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
    afterEvent: function (chart, event) {
        if (!chart.zoomBox)
            chart.zoomBox = {};
        if (event.type === "mousedown") {
            if (event.native.ctrlKey) {
                chart.zoomBox.pixelInitial = {x: event.x, y: event.y};
                chart.zoomBox.scaleInitial = getScaledValues(chart, event);
                chart.zoomBox.dragging = true;
                chart.zoomBox.pixelFinal = null;
                chart.zoomBox.scaleFinal = null;
            }
            else {
                chart.zoomBox.pixelInitial = null;
                chart.zoomBox.scaleInitial = null;
                chart.zoomBox.dragging = false;
            }
        }
        else if (event.type === "mouseup") {

            var tClick = Date.now();
            if (chart.zoomBox.prevClick > 0 && (tClick - chart.zoomBox.prevClick) < 300) {
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
        else if (event.type === "mousemove" && chart.zoomBox && chart.zoomBox.dragging) {
            chart.zoomBox.pixelCurrent = {x: event.x, y: event.y};
            chart.zoomBox.scaleCurrent = getScaledValues(chart, event);
        }
    },
    beforeDraw: function (chart, easing) {
        var ctx = chart.chart.ctx;
        var deltaLimit = 30;
        if (chart.zoomBox && chart.zoomBox.dragging && chart.zoomBox.pixelInitial && chart.zoomBox.pixelCurrent) {

            var dX = chart.zoomBox.pixelCurrent.x - chart.zoomBox.pixelInitial.x;
            var dY = chart.zoomBox.pixelCurrent.y - chart.zoomBox.pixelInitial.y;

            // Rectangle mode
            if (Math.abs(dX) > deltaLimit && Math.abs(dY) > deltaLimit) {
                chart.zoomBox.zoomMode = 'xy';
                ctx.save();
                ctx.fillStyle = "#e0d7d999";
                ctx.fillRect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.lineWidth = "1";
                ctx.strokeStyle = "red";
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.stroke();
                ctx.rect(chart.zoomBox.pixelInitial.x, chart.zoomBox.pixelInitial.y, dX, dY);
                ctx.restore();
            }
            // H-Zoom only (vertical lines)
            else if (Math.abs(dX) > Math.abs(dY)) {
                chart.zoomBox.zoomMode = 'x';
                ctx.save();
                ctx.fillStyle = "#e0d7d999";
                ctx.fillRect(chart.zoomBox.pixelInitial.x, 0, dX, 1e6);
                ctx.lineWidth = "1";
                ctx.strokeStyle = "red";
                ctx.rect(chart.zoomBox.pixelInitial.x, 0, dX, 1e6);
                ctx.stroke();
                ctx.restore();
            }
            // V-Zoom only (horizontal lines)
            else {
                chart.zoomBox.zoomMode = 'y';
                ctx.save();
                ctx.fillStyle = "#e0d7d999";
                ctx.fillRect(0, chart.zoomBox.pixelInitial.y, 1e6, dY);
                ctx.lineWidth = "1";
                ctx.strokeStyle = "red";
                ctx.rect(0, chart.zoomBox.pixelInitial.y, 1e6, dY);
                ctx.stroke();
                ctx.restore();
            }
        }
    }
});