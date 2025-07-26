/*
 * Copyright 2025 David Main
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// ensure that host is defined, and leave it blank so that by default the
// host that served the HTML that caused this script file to be loaded is
// the host that is contacted for API calls

let host = "";

// for development purposes, one of the following lines can be uncommented
// so that a specific board is targeted for API calls regardless of the
// host that served up the HTML (also required if you are loading the
// HTML from a file instead of a URL from a server)

// let host = "http://192.168.7.65";  /* sptest */
// let host = "http://192.168.7.159"; /* sp3 */

/* this function is called when the page is loaded */

(function () {
    initButtons();
    setBackButtonVisible(false);
    requestServerSentEvents();
})();

var response = null;
var x = null;

/****
 * Add the "last()" method to the built-in Array class so that you
 * don't have to do this sort of thing a[a.length - 1]
 */

if (!Array.prototype.last) {
    Array.prototype.last = function () {
        return this[this.length - 1];
    };
}

/****
 * Add the "addDays()" method to the built-in Date class for
 * convenience.
 */

if (!Date.prototype.addDays) {
    Date.prototype.addDays = function(days) {
        var date = new Date(this.valueOf());
        date.setDate(date.getDate() + days);
        return date;
    }
}

/* 
    * The following two convenience functions are intended to make code that
    * targets specific portions of the DOM easier to write.  For example, to
    * find all of the buttons for the zones_view, you can use the following:
    * 
    *     dqsa("#zones_view button")
    * 
    * which is equivalent to:
    * 
    *    document.querySelectorAll("#zones_view button")
    * 
    * It is also possible to use the fact that all "id" values that have
    * JavaScript compatible names are automatically added to the global
    * namespace.  For example, the following is also equivalent to the above:
    * 
    *     zones_view.querySelectorAll("button")
    * 
    * In the end, dqsa() is still shorter, and at least to some people smarter
    * than me, more reliable because there is no knowing when the global variable
    * name "pollution" might change.
    * 
    * I am not sure which is better to use, and I have tried all approaches above.
    * Maybe sometime I will settle on one and refactor the code to be consistent.
    */

/* dqsa - shortcut function for: "document.querySelectorAll()" */

function dqsa(selector) {
    return document.querySelectorAll(selector);
}

/* dqs - shortcut function for: "document.querySelector()" */

function dqs(selector) {
    return document.querySelector(selector);
}

function initButtons() {
    // main view buttons

    dqsa("#main_view button").forEach(el => {
        el.addEventListener("click", mainButtonClick);
    });

    // set up the back button

    dqs("#display_area div.view_header span i.bi-arrow-left-circle")
        .addEventListener("click", popView);

    // zones view buttons

    dqsa("#zones_view button").forEach(el => {
        el.addEventListener('click', zoneButtonClick);
    });

    // schedule view buttons

    dqsa("#schedule_view .zn_btn").forEach(el => {
        el.addEventListener('click', function (event) {
            event.preventDefault();
            this.classList.add("btn-info");
        });
    });

    dqs(".clr_btn").addEventListener('click', function (event) {
        document.querySelectorAll(".zn_btn.btn-info")
            .forEach(el => {el.classList.remove("btn-info")});
    });

    dqsa(".min_btn").forEach(el => {
        el.addEventListener('click', minuteButtonClick);
    }); 

    dqs("#schedule_view button.ctrl_btn").onclick = (function () {
        refreshStatusArea('schedule_view');
    });

    // add a submit processing function to the form in the cycle_edit_view

    dqs("#cycle_edit_view form").addEventListener("submit", cycleEditViewFormSubmit);

    // Set up display panel control buttons

    btn_display_cancel.onclick = (function() {
        getUrl("/schd/cancel", removeFocus);
    });

    btn_display_skip.onclick = (function() {
        getUrl("/schd/skip", removeFocus);
    });

    btn_display_pause.onclick = (function() {
        if (div_display_panel_state.innerText === "paused") {
            getUrl("/schd/resume", removeFocus);
        } else {
            getUrl("/schd/pause", removeFocus);
        }
    });

    btn_seasonalAdjustment_submit.onclick = seasonalAdjustmentSubmit;

    // Add an onclick handler to the Bottom button on the Log view

    btn_log_bottom.onclick = (function() {
        log_contents_bottom.scrollIntoView();
    });
}

/**
 * removeFocus()
 * 
 * This function is used to remove the focus outline on a button that
 * should have only a momentary action when tapped.
 */
function removeFocus() {
    // protect from an error using the Optional Chaining operator
    dqs("button:focus")?.blur();
}

/**
 * requestServerSentEvents()
 * 
 * Function sets up for receiving Server Sent Events from the
 * controller.  It is invoked by the initial load function.
 */
function requestServerSentEvents() {
    const evtSource = new EventSource(host + "/sse");

    evtSource.onmessage = (event) => {
        const data = JSON.parse(event.data);
        response = data.apiStatus;
        updateStatusToUI();
    };

    evtSource.addEventListener("stop", (event) => {
        evtSource.close();
        console.log(
            "EventSource closed - data:",
            JSON.parse(event.data)
        );
        alert("EventSource closed");
    });

    evtSource.onerror = (event) => {
        console.log("EventSource had an error:");
        console.log((event) ? event : "event was undefined");
    }
}

/**
 * updateStatusToUI()
 * 
 * Takes whatever is found in the global response variable and updates all relevant
 * portions of the UI to the latest data.  The idea is that this can be called at
 * any time, and if there is nothing in the response global object, it will invoke
 * the /status API in order to get fresh data.
 * 
 * This function is invoked when a Server Sent Event is received, as configured in
 * requestServerSentEvents().
 */
function updateStatusToUI() {
    if (!response) {
        getUrl("/status", updateStatusToUI);
        return;
    }

    console.log(
        "on: " + response.on + " " +
        "schedulerState: " + response.schedulerState + " " +
        "schedule: " + response.schedule + " " +
        "currCycle: " + response.currCycle + " " +
        "nextCycle: " + response.nextCycle
    );

    if (viewList.last().id === "main_view") {
        system_title.innerText = `${response.hostname} Controls`;
    }
    head_title.innerText = response.hostname;

    if (response.currCycle) {
        div_display_panel_1.innerText = "Current cycle: " + response.currCycle;
        div_display_panel_2.innerText = "Zone: " + ((response.on.length == 0) ? "-" : response.on);
        div_display_panel_state.innerText = response.schedulerState;
        div_display_buttons.hidden = false;
    } else
    if (response.schedule?.length) {
        div_display_panel_1.innerText = "Schedule: " + response.schedule;
        div_display_panel_2.innerText = "Zone: " + response.on;

        // hide from the user the situation where the schedule has items
        // in it, but the schedule has not yet started to run, so the 
        // state is "stopped" for a moment before it transitions to
        // "running"

        if (response.schedulerState !== "stopped") {
            div_display_panel_state.innerText = response.schedulerState;
        } else {
            div_display_panel_state.innerText = '';
        }
        div_display_buttons.hidden = false;
    } else
    if (response.nextCycle) {
        div_display_panel_1.innerText = "Next cycle: " + response.nextCycle;
        div_display_panel_2.innerText = response.startDateTime;
        div_display_panel_state.innerText = '';
        div_display_buttons.hidden = true;
    } else
    if (!response.currCycle && !response.nextCycle && response.resume === "system off") {
        div_display_panel_1.innerText = "System Off";
        div_display_panel_2.innerText = "Tap System Hold to resume operation";
        div_display_panel_state.innerText = "";
        div_display_buttons.hidden = true;
    }

    div_display_panel_time.innerText = response.time.substring(0,5);

    if (response.schedulerState === "paused") {
        svg_pause.classList.toggle("invisible");
        svg_play.classList.toggle("invisible");
    } else {
        if (!svg_play.classList.contains("invisible")) {
            svg_play.classList.add("invisible");
        }
        if (svg_pause.classList.contains("invisible")) {
            svg_pause.classList.remove("invisible");
        }
    }
}

/***
 * todo - remove this function when better ones have replaced it
 ***/

function refreshStatusArea(view_name) {
    getUrl("/status", function(response) {
        updateStatusArea(response, "#" + view_name + " div.status_area");
    });
}

function mainButtonClick(event) {
    let view = this.innerText.toLowerCase().replaceAll(" ", "_");

    if (view === "zones") {
        getUrl("/status", function(response) {
            console.log("response.on=" + response.on);
            response["on"].forEach(function(zone) {
                console.log("zone=" + zone);
                let btn = dqs("#zones_view button[data-value='" + zone + "']");
                btn.classList.add("btn-success");
                btn.classList.remove("btn-primary");
            });
            pushView(view);
        });
        return;
    } else
    if (view === "schedule") {
        dqs("#schedule_view div.status_area").innerHTML = "";
    } else
    if (view === "cycles") {
        updateCyclesView();
        pushView(view);
        return;
    } else
    if (view === "status") {
        getUrl("/status", function(response) {
            updateStatusArea(response, "#status_view div.status_area");
        });
    } else
    if (view === "system_settings") {
        updateSystemSettingsView();
    } else
    if (view === "log") {
        // clear the contents of the view first
        log_contents.innerText = "Retrieveing log...";

        getUrl("/log/show", function(response) {
            if (response.hasOwnProperty("status")) {
                if (response.status === "error") {
                    if (response.msg === "file '/log.dat' not found") {
                        log_contents.innerText = "(log empty)";
                    } else {
                        log_contents.innerText = response.msg;
                    }
                }
                return;
            }
            log_contents.innerText = response;
            log_contents_bottom.scrollIntoView();
        });
    } else
    if (view === "seasonal_adjustment") {
        getUrl("/adj", function(response) {
            seasonalAdjustmentField.value = response.adj;
        });
    } else
    if (view === "system_hold") {
        getUrl("/hold", function(response) {
            updateSystemHoldView(response);
            pushView(view);
        });
        return;
    }

    pushView(view);
}

/**
 * Find the view id starting at the anchor and moving upwards in
 * the HTML until a div is found with an id ending in "_view".
 * 
 * @param anchor - starting anchor point from which to commence 
 *      the search.  This can be any HTML node and traversal is 
 *      done using its parentNode.
 */

function findView(anchor) {
    while (true) {
        if (anchor.hasAttribute("id") && anchor.id.endsWith("_view")) {
            return anchor
        }
        anchor = anchor.parentNode;

        if (anchor.nodeName === "BODY") {
            console.log("error: findViewID() encountered <body> - no view id found");
            return null
        }
    }
}

/**
 * viewList defines a stack of "views".  A "view" is a div element
 * that presents some portion of the UI to the user, named with a
 * unique id value that ends with "_view".  Views are hidden and
 * unhidden by pushing them onto the viewList and later popping
 * them back off in a coordinated way to simulate the appearance 
 * of a multi-page interface.
 */

var viewList = [dqs("#main_view")];

function setViewTitle(title) {
    system_title.innerText = title;
}

function setBackButtonVisible(val) {
    let backBtn = dqs("#display_area div.view_header span i.bi-arrow-left-circle");
    
    backBtn.hidden = !val;
}

function removeRightButton(btn) {
    var classToRemove = Array.from(btn.classList).filter(c => c.startsWith("bi-"));

    if (classToRemove.length > 0) {
        btn.classList.remove(classToRemove);
    }
}

function handleRightButton(viewNode) {
    const rightBtn = dqs("div.view_header .right-button");
    var rightBtnHidden = true;

    removeRightButton(rightBtn);

    if (viewNode.dataset.rightBtnClass) {
        rightBtn.classList.add(viewNode.dataset.rightBtnClass);
        rightBtnHidden = false;
    }

    if (viewNode.dataset.rightBtnOnclick) {
        rightBtn.onclick = eval("(function(){" + viewNode.dataset.rightBtnOnclick + "})");
        rightBtnHidden = false;
    }

    rightBtn.hidden = rightBtnHidden;
}

/**
 * Make a new view visible and push the current view onto the
 * viewList so that the user can return to it later.
 * 
 * @param newViewName - the name of the new view to present.  If
 *      the name doesn't end in "_view", then it will be suffixed
 *      automatically.
 */

function pushView(newViewName, title) {
    if (!newViewName.endsWith("_view")) {
        newViewName += "_view";
    }

    let newView = dqs("#" + newViewName);
    let oldView = viewList.last();

    oldView.dataset["viewTitle"] = system_title.innerText;

    if (!newView) {
        console.log("error: newViewName '" + newViewName + "' not found");
        return;
    }

    setViewTitle((title === undefined) ? newView.dataset.title : title);
    setBackButtonVisible(viewList.length > 0);
    handleRightButton(newView);

    oldView.hidden = true;
    newView.hidden = false;

    viewList.push(newView);
}

/**
 * Replace the current view with the previous one pushed on the
 * view stack.
 */

function popView() {
    viewList.pop().hidden = true;

    let lastView = viewList.last();

    setViewTitle(lastView.dataset.viewTitle);
    setBackButtonVisible(viewList.length > 1);
    handleRightButton(lastView);

    lastView.hidden = false;
}

function zoneButtonClick(event) {
    let zone = this.innerText;
    var url = "/zone/";
    var statusArea = dqs("#zones_view .status_area");

    statusArea.innerText = "";

    if (zone === "Off") {
        url += "all/off";
    } else {
        url += zone + "/on";
    }

    getUrl(url, function(response) {
        statusArea.innerText = JSON.stringify(response);

        if (zone === "Off") {
            document.querySelectorAll("#zones_view button").forEach(function(btn) {
                btn.classList.add("btn-primary");
                btn.classList.remove("btn-success");
            });
        } else {
            let btn = document.querySelector("#zones_view button[data-value='" + zone + "']");
            btn.classList.add("btn-success");
            btn.classList.remove("btn-primary");
        }
    });
}

function minuteButtonClick(event) {
    event.preventDefault();

    this.blur();

    let minutes = this.getAttribute("data-minutes");

    // searching for .zn_btn that also have the .btn-info class
    // means that they have been "flipped on" or selected by the
    // user -- these are the zones which should be scheduled.

    let zoneBtns = document.querySelectorAll(".zn_btn.btn-info");
    let scheduleDiv = document.querySelector("#schedule_div");

    // if one or more zone_btns was selected

    if (zoneBtns.length > 0) {
        // build the URL to start a zone (or selected zones)

        let url = "/schd/" + 
            Array.from(zoneBtns).map(b => b.innerText).join(",") +
            "/" + minutes;

        // get the URL, and handle the outcome

        getUrl(url, function (response) {
            if (response["status"] !== "ok") {
                alert(response["msg"]);
            }
        });

        removeClass("button.zn_btn.btn-info", "btn-info");
        // document.querySelectorAll("button.zn_btn.btn-info")
        //     .forEach(el => el.classList.remove("btn-info"));
    } else {
        alert("Select a Zone first, then press a Minutes button.");
    }
}

function removeClass(selectorExpr, className) {
    dqsa(selectorExpr).forEach(el => el.classList.remove(className));
}

function updateCyclesView() {
    getUrl("/cycles.json", function(response) {
        let content = dqs("#cycles_view .view_content");

        if (response.cycles?.length) {
            var template = [];

            template.push('<div class="d-grid gap-2">');

            response["cycles"].forEach(function (ci) {
                template.push(
                    '<button type="button" class="btn btn-primary btn-lg"' +
                        'onclick="cycleButtonClick(this)">' +
                        ci["name"] + '</button>'
                );
            });

            template.push('</div>');

            var htmlString = template.join('');

            document.querySelector("#cycles_view .view_content").innerHTML = htmlString;
        } else {
            content.innerText = "No cycles found";
        }
    });
}

function updateSystemSettingsView() {
    getUrl("/logic", function(response) {
        dqs("#logicModeField").value = response["logic"];
    });
    getUrl("/oe", function(response) {
        dqs("#outputEnableField").value = response["oe"]
    });
}

function updateSystemHoldView(response) {
    // hide all system_hold_view sub-views (div)
    [...dqs("#system_hold_view div.view_content").children].forEach(el => {el.hidden = true});
    
    if (response.holdDays == 0) {
        // build 6 buttons that can quickly engage a system hold
        // for the next 6 days, plus a custom button and an
        // indefinite "Turn off" system hold button.

        const today = new Date();
        const dtf = new Intl.DateTimeFormat(
            'en-GB', {
                weekday: 'short', 
                month: 'short', 
                day: 'numeric'
            });
        const holdVals = Array.from(
            {length:6}, 
            (_, i) => 
                '<div class="col">' +
                '<button type="button" class="btn btn-primary btn-lg w-100" ' + 
                    'data-value=' + (i + 1) + ' onclick="systemHoldButtonClick(this)">' +
                    dtf.format(today.addDays(i + 1)) + 
                    '</button>' +
                '</div>'
            ).concat(
                '<div class="col">' +
                '<button type="button" class="btn btn-primary btn-lg w-100" ' +
                'data-value="custom" onclick="systemHoldButtonClick(this)">Custom</button>' +
                '</div>',
                '<div class="col">' +
                '<button type="button" class="btn btn-danger btn-lg w-100" ' +
                'data-value="turn off" onclick="systemHoldButtonClick(this)">Turn Off</button>' +
                '</div>',
            )

        system_hold_start.innerHTML = holdVals.join('');
        system_hold_start.hidden = false;
        holdDaysField.value = '';
    } else
    if (response.holdDays == -1) {
        system_turn_on.hidden = false;
    } else {
        dqs("#system_hold_cancel span").innerText = response.resume;
        system_hold_cancel.hidden = false;
    }
}

function systemHoldButtonClick(btn) {
    const btnVal = btn.dataset.value;

    console.log("btnVal=" + btnVal);

    [...dqs("#system_hold_view div.view_content").children]
        .forEach(el => {el.hidden = true});

    if (btnVal === "custom") {
        system_hold_custom.hidden = false;
    } else
    if (btnVal === "turn on" || btnVal === "cancel hold") {
        getUrl("/hold/0", function(response) {
            if (response.status === "ok") {
                system_hold_result.innerText = "Normal system operation resumed.";
                system_hold_result.hidden = false;
            } else {
                alert(response?.msg);
            }
        });
    } else
    if (btnVal == "turn off") {
        getUrl("/hold/-1", function(response) {
            system_turn_on.hidden = false;
        });
    }
    else /* date button or custom form submit */ {
        const holdVal = (btnVal === "custom submit") ?
            holdDaysField.value : btnVal;

        getUrl("/hold/" + holdVal, function(response) {
            dqs("#system_hold_cancel span").innerText = response.resume;
            system_hold_cancel.hidden = false;
        });
    }
}

function cycleButtonClick(btn) {
    const m = new bootstrap.Modal("#cycle_modal", {});
    dqs("#cycle_modal_title").innerHTML = "Cycle:&nbsp;" + btn.innerText;
    dqsa("#cycle_modal button.btn").forEach(el => {
        if (el.innerText === "Edit") {
            el.onclick = function () {
                editCycle(btn.innerText, btn);
                m.hide();
            };
        } else
        if (el.innerText === "Run") {
            el.onclick = function () {
                let cycleRunURL = encodeURI("/cycle/" + btn.innerText + "/run");
                getUrl(cycleRunURL, function(response) {
                    console.log(response);
                    m.hide();
                });
            };
        } else
        if (el.innerText === "Delete") {
            el.onclick = function () {
                deleteUrl("/cycle", {name: btn.innerText}, function (response) {
                    console.log(response);
                    updateCyclesView();
                    m.hide();
                });
            }
        } else
        if (el.innerText === "Copy") {
            el.onclick = function () {
                editCycle(btn.innerText, btn, function () {
                    nameField.value += " Copy"
                });
                m.hide();
            }
        }
    });
    m.show();
}

function scheduleItemDelete(el) {
    el.closest("div").remove();
}

function scheduleItemAdd(el) {
    el.classList.remove("bi-plus-circle");
    el.classList.remove("text-success");
    el.classList.add("bi-x-circle");
    el.classList.add("text-danger");
    el.setAttribute("onclick", "scheduleItemDelete(this)");

    dqs("#cycle_edit_schedule").lastElementChild.insertAdjacentHTML(
        "afterend",
        Array.from(scheduleItemHtml()).join('')
    );
}

function scheduleItemHtml(zones, runTime) {
    // ensure the parameters have empty strings if they are undefined
    zones = zones || '';
    runTime = runTime || '';

    return [
        '<div class="mb-2">',
        '<input type="text" name="zones" pattern="[0-9,]*" class="zone_input"' +
            ((zones) ? (' value="' + zones + '"') : '') + '>',
        '<input type="text" name="runTime" pattern="\\d*" class="time_input"' + 
            ((runTime) ? (' value="' + runTime + '"') : '') + '>',
        '<span style="width: 50px"></span>',
        (!zones && !runTime) ?
            '<i class="bi bi-plus-circle text-success" onclick="scheduleItemAdd(this)"></i>' :
            '<i class="bi bi-x-circle text-danger" onclick="scheduleItemDelete(this)"></i>',
        '</div>'
    ]
}

function editCycle(cycleName, el, addlFunc) {
    getUrl(encodeURI("/cycle/" + cycleName), function (response) {
        dqsa("#cycle_edit_view input").forEach(el => {
            let fn = el.id.replace("Field", "");
            el.value = response[fn];
        });

        // clear all radio and checkbox buttons in preparation for
        // setting them to whatever was in the response -- these
        // happen all to be contained inside divs with the
        // btn-group class

        dqsa("#cycle_edit_view div.btn-group input")
            .forEach(el => {el.checked = false;});

        // now set the cycle type
        dqs("#cycle_edit_view input[data-value='" + response["type"] + "']")
            .checked = true;

        // now set the days correctly
        response["days"].forEach(d => {
            dqs("#cycle_edit_view input[data-value='" + d + "']")
                .checked = true;
        });

        // by concatenating a list of empty strings to the array in
        // response["schedule"], an empty item can be created so
        // that there are fields waiting to be entered in order to
        // add a new schedule item

        dqs("#cycle_edit_schedule").innerHTML = 
            response["schedule"].concat([['','']])
                .map(si => scheduleItemHtml(si[0], si[1]).join(''))
                .join('');

        // run the addlFunc if it was provided -- this allows invokers
        // to request additional steps to be taken after the form has
        // been prepared for display

        if (addlFunc) {
            addlFunc();
        }
        pushView("cycle_edit", "Edit " + cycleName);
    });
}

function addNewCycle () {
    // clear out the cycle_edit_schedule <div>
    [...dqs("#cycle_edit_schedule").children]
        .slice(1)
        .forEach(el => { el.remove(); });
    
    // clear all radio and checkbox buttons in preparation for
    // setting them to whatever was in the response -- these
    // happen all to be contained inside divs with the
    // btn-group class

    dqsa("#cycle_edit_view div.btn-group input")
        .forEach(el => {el.checked = false;});

    // add an empty field to start inserting a schedule
    dqs("#cycle_edit_schedule").innerHTML = scheduleItemHtml().join('');
    
    // clear out all input fields in the view
    dqsa("#cycle_edit_view input").forEach(el => { el.value = ""; });

    pushView("cycle_edit", "Add Cycle");
}

function cycleEditViewFormSubmit (event) {
    event = event || window.event;
    event.preventDefault();

    const cycleEditData = new FormData(dqs("#cycle_edit_view form"));
    const cycleEntries = Array.from(cycleEditData.entries());
    const values = Object.fromEntries(cycleEntries);
    const zones = cycleEntries
                    .filter(e => e[0] === "zones")
                    .map(z => z[1].split(",").map(i => parseInt(i, 10)));
    const runTimes = cycleEntries
                    .filter(e => e[0] === "runTime")
                    .map(r => parseInt(r[1], 10));

    // this is how I originally obtained an array of ints for the days
    // that where hand-entered into the text box
    // values["days"] = values["days"].split(",").map(i => parseInt(i, 10));

    // now, "days" is obtained by obtaining all checked input fields with
    // name="dayOfWeek" -- this even works across the two divs that 
    // contain separate rows of buttons -- by just obtaining the value of
    // their data-value attribute

    values["days"] = [...dqsa("#cycle_edit_view input[name='dayOfWeek']:checked")]
        .map(el => parseInt(el.dataset["value"], 10));

    // it's even easier to grab the type value

    values["type"] = dqs("#cycle_edit_view input[name='cycleType']:checked")
        ?.dataset["value"];

    if (!values["type"]) {
        alert("Select a cycle type");
        return;
    }

    values["hour"] = parseInt(values["hour"], 10) || 0;
    values["min"] = parseInt(values["min"], 10) || 0;
    values["first"] = parseInt(values["first"], 10) || 1;
    values["count"] = parseInt(values["count"], 10) || 1;

    // zip zones and runTimes together.  If zones = [[3], [2,6]], then
    // zones.entries() = [ [0,[3]], [1,[2,6]], [2,[Nan]] ]
    // In other words, Array.entries() works like Python's enumerate()
    // function, where it creates tuples as it iterates, with the first
    // item being the index, and the second item being the iterator 
    // value.  Empty input fields will come through as the value NaN,
    // so these are filtered out by looking at the iterator value, 
    // which will be an array, and then seeing if the first item in
    // the array is NaN.

    values["schedule"] = Array.from(zones.entries())
                            .filter(e => !isNaN(e[1][0]))
                            .map(e => [e[1], runTimes[e[0]]]);

    delete values["zones"];
    delete values["runTime"];

    postUrl("/cycle", values, function (response) {
        if (response["status"] !== "ok") {
            alert(response["msg"]);
            return;
        }
        updateCyclesView();

        // this is a horrible cludge, but I want to update the status display
        response = undefined;

        popView();
    });
}

function seasonalAdjustmentSubmit() {
    getUrl("/adj/" + seasonalAdjustmentField.value, function (response) {
        if (response.status === "ok") {
            alert("Seasonal adjustment set to " + response.adj);
            popView();
        }
    });
}

function schedulePost(url) {
    let items = document.querySelector("#schedule_div").innerText;
    var data = {
        schedule: JSON.parse("[" + items + "]")
    };
    postUrl(url, data, function (response) {
        updateStatusArea(response, "#schedule_view div.status_area");

        let status;

        if (response["status"] === "ok") {
            if (url.endsWith("set")) {
                status = "Schedule started";
            } else {
                status = "Schedule appended";
            }
        } else {
            status = "Error";
        }

        document.querySelector("#schedule_div").innerText = status;
    });
}

function updateStatusArea(response, selectorExpr) {
    var template = [];

    template.push(
        '<table class="table table-sm table-bordered">',
            '<thead>',
                '<tr>',
                    '<th>key</th>',
                    '<th>value</th>',
                '</tr>',
            '</thead>',
            '<tbody>'
    );

    Object.keys(response).forEach(function (key) {
        template.push(
            '<tr>',
                '<td>' + key + '</td>',
                '<td>' + response[key] + '</td>',
            '</tr>'
        );
    });

    template.push(
        '</tbody>',
        '</table>'
    );

    var htmlString = template.join('');

    document.querySelector(selectorExpr).innerHTML = htmlString;
}

function restartController() {
    getUrl("/restart", function (response) {
        const m = new bootstrap.Modal("#simple_modal", {});
        h4_simple_modal_title.innerText = "Restart Controller";
        div_simple_modal_body.innerHTML = response["status"];
        m.show();
    });
}

function sendRequest(method, path, data, onload_func) {
    let url = host + path;
    console.log(url);
    var xhr = new XMLHttpRequest();
    xhr.open(method, url, true);
    xhr.onload = function (e) {
        if (xhr.readyState === 4) {
            if (xhr.status === 200) {
                if (onload_func === undefined) {
                    console.log("no onload_func defined - status: " + xhr.responseText);
                } else {
                    try {
                        response = JSON.parse(xhr.responseText);
                    } catch (error) {
                        response = xhr.responseText;
                    }
                    onload_func(response);
                }
            } else {
                console.error(xhr.statusText);

                div_display_panel_time.innerText = '';
                div_display_panel_1.innerText = 'STATUS: ' + xhr.status;
                div_display_panel_2.innerText = xhr.statusText;
                div_display_panel_state.innerText = '';
            }
        }
    };
    xhr.onerror = function (e) {
        console.error(xhr.statusText);
    }

    if (method === "GET") {
        xhr.send(null);
    } else {
        xhr.send(JSON.stringify(data));
    }
}

function getUrl(path, onload_func) {
    sendRequest("GET", path, null, onload_func);
}

function postUrl(path, data, onload_func) {
    sendRequest("POST", path, data, onload_func);
}

function deleteUrl(path, data, onload_func) {
    sendRequest("DELETE", path, data, onload_func);
}