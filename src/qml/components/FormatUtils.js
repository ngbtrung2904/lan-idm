.pragma library

function formatBytes(bytes) {
    if (bytes === undefined || bytes === null || isNaN(bytes) || bytes <= 0) {
        return "0 B";
    }
    var kb = 1000;
    var mb = kb * 1000;
    var gb = mb * 1000;
    var tb = gb * 1000;

    if (bytes >= tb) {
        return (bytes / tb).toFixed(2) + " TB";
    }
    if (bytes >= gb) {
        var g = bytes / gb;
        if (g >= 10) {
            return g.toFixed(1) + " GB";
        }
        return g.toFixed(2) + " GB";
    }
    if (bytes >= mb) {
        var m = bytes / mb;
        return (m >= 10 ? Math.round(m) : m.toFixed(1)) + " MB";
    }
    if (bytes >= kb) {
        return Math.round(bytes / kb) + " KB";
    }
    return Math.round(bytes) + " B";
}

function formatFileCount(count) {
    if (count === undefined || count === null || isNaN(count)) {
        return "0 files";
    }
    var n = Math.floor(count);
    var str = n.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    return str + (n === 1 ? " file" : " files");
}

function formatRate(bytesPerSec) {
    if (bytesPerSec === undefined || bytesPerSec === null || isNaN(bytesPerSec) || bytesPerSec <= 0) {
        return "0 MB/s";
    }
    var mb = 1000 * 1000;
    var kb = 1000;
    if (bytesPerSec >= mb) {
        var m = bytesPerSec / mb;
        return (m >= 10 ? Math.round(m) : m.toFixed(1)) + " MB/s";
    }
    if (bytesPerSec >= kb) {
        return Math.round(bytesPerSec / kb) + " KB/s";
    }
    return Math.round(bytesPerSec) + " B/s";
}

function formatEta(seconds) {
    if (seconds === undefined || seconds === null || isNaN(seconds) || seconds < 0) {
        return "";
    }
    if (seconds < 45) {
        return "Less than 1 min";
    }
    var mins = Math.round(seconds / 60);
    if (mins < 60) {
        return "About " + mins + " min";
    }
    var hrs = Math.floor(mins / 60);
    var rem = mins % 60;
    if (rem === 0) {
        return "About " + hrs + " hr";
    }
    return "About " + hrs + " hr " + rem + " min";
}

function formatDateTimeDisplay(dt) {
    if (!dt || isNaN(dt.getTime())) {
        return "";
    }
    var pad = function(v) { return v < 10 ? "0" + v : "" + v; };
    var mm = pad(dt.getMonth() + 1);
    var dd = pad(dt.getDate());
    var yyyy = dt.getFullYear();
    var hours = dt.getHours();
    var ampm = hours >= 12 ? "PM" : "AM";
    var h12 = hours % 12;
    if (h12 === 0) h12 = 12;
    var hh = pad(h12);
    var min = pad(dt.getMinutes());
    return mm + "/" + dd + "/" + yyyy + ", " + hh + ":" + min + " " + ampm;
}
