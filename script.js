/* ============================================================
   AERIS — AIR INTELLIGENCE
   Firestore Dashboard Controller
   ============================================================ */


/* ============================================================
   CONFIGURATION
   ============================================================ */

const FIRESTORE_URL =
  "https://firestore.googleapis.com/v1/projects/" +
  "aeris-8af63/databases/(default)/documents/" +
  "devices/device_01";

const REFRESH_INTERVAL = 5000;


/* ============================================================
   DOM HELPER
   ============================================================ */

const $ = (selector) => {
  return document.querySelector(selector);
};


/* ============================================================
   FIRESTORE VALUE READER
   ============================================================ */

function fieldValue(field) {

  if (!field) {
    return null;
  }

  // Firestore integer
  if (field.integerValue !== undefined) {
    return Number(field.integerValue);
  }

  // Firestore double
  if (field.doubleValue !== undefined) {
    return Number(field.doubleValue);
  }

  // Firestore string
  if (field.stringValue !== undefined) {
    return field.stringValue;
  }

  // Firestore boolean
  if (field.booleanValue !== undefined) {
    return field.booleanValue;
  }

  // Firestore timestamp
  if (field.timestampValue !== undefined) {
    return field.timestampValue;
  }

  return null;
}


/* ============================================================
   AQI CATEGORIES
   ============================================================ */

const AQI_CATEGORIES = [

  {
    max: 50,
    label: "GOOD",
    color: "#4ad991"
  },

  {
    max: 100,
    label: "MODERATE",
    color: "#e8c94a"
  },

  {
    max: 150,
    label: "UNHEALTHY FOR SENSITIVE GROUPS",
    color: "#f0973d"
  },

  {
    max: 200,
    label: "UNHEALTHY",
    color: "#ec5b52"
  },

  {
    max: 300,
    label: "HAZARDOUS",
    color: "#ec5b52"
  }

];


/* ============================================================
   AQI CLASSIFICATION
   ============================================================ */

function classifyAQI(value) {

  return (
    AQI_CATEGORIES.find(
      (category) => value <= category.max
    )
    ||
    AQI_CATEGORIES[
      AQI_CATEGORIES.length - 1
    ]
  );

}


/* ============================================================
   NUMBER FORMATTER
   ============================================================ */

function formatValue(value, decimals = 1) {

  if (
    value === null ||
    value === undefined ||
    value === "" ||
    Number.isNaN(Number(value))
  ) {
    return "--";
  }

  return Number(value).toFixed(decimals);

}


/* ============================================================
   TEXT UPDATE
   ============================================================ */

function setText(selector, value, fallback = "--") {

  document
    .querySelectorAll(selector)
    .forEach((element) => {

      if (
        value === null ||
        value === undefined ||
        value === ""
      ) {

        element.textContent = fallback;

      } else {

        element.textContent = value;

      }

    });

}


/* ============================================================
   AQI GAUGE
   ============================================================ */

function renderGauge(container, value) {

  if (!container) {
    return;
  }


  const aqi =
    Math.max(
      0,
      Math.min(
        Number(value) || 0,
        300
      )
    );


  const category =
    classifyAQI(aqi);


  const width = 240;
  const height = 150;

  const cx = 120;
  const cy = 130;

  const radius = 96;


  const angle =
    Math.PI -
    (aqi / 300) *
    Math.PI;


  const point = (
    theta,
    r = radius
  ) => ({

    x:
      cx +
      r *
      Math.cos(theta),

    y:
      cy -
      r *
      Math.sin(theta)

  });


  const needle =
    point(
      angle,
      78
    );


  const ticks = [

    50,
    100,
    150,
    200,
    300

  ]

    .map((tick) => {

      const theta =
        Math.PI -
        (tick / 300) *
        Math.PI;


      const outer =
        point(
          theta,
          106
        );


      const inner =
        point(
          theta,
          88
        );


      return `

        <line
          x1="${outer.x}"
          y1="${outer.y}"
          x2="${inner.x}"
          y2="${inner.y}"

          stroke="${category.color}"

          stroke-opacity=".7"

          stroke-width="2"
        />

      `;

    })

    .join("");


  const gaugeTrack = `

    <path

      d="
        M 24 130
        A 96 96 0 0 1 216 130
      "

      fill="none"

      stroke="${category.color}"

      stroke-opacity=".24"

      stroke-width="14"

    />

  `;


  const activeEnd =
    point(angle);


  const activeArc =

    aqi > 0

      ? `

        <path

          d="
            M 24 130
            A 96 96 0 0 1
            ${activeEnd.x}
            ${activeEnd.y}
          "

          fill="none"

          stroke="${category.color}"

          stroke-width="14"

          stroke-linecap="round"

        />

      `

      : "";


  container.innerHTML = `

    <svg

      viewBox="0 0 ${width} ${height}"

      role="img"

      aria-label="AQI ${formatValue(aqi, 0)}"

    >

      ${gaugeTrack}

      ${activeArc}

      ${ticks}


      <line

        x1="120"
        y1="130"

        x2="${needle.x}"
        y2="${needle.y}"

        stroke="${category.color}"

        stroke-width="3"

        stroke-linecap="round"

      />


      <circle

        cx="120"
        cy="130"

        r="6"

        fill="${category.color}"

      />


      <text

        x="120"
        y="147"

        text-anchor="middle"

        fill="${category.color}"

        font-size="9"

        font-family="JetBrains Mono, monospace"

      >

        AQI / 300

      </text>

    </svg>

  `;

}


/* ============================================================
   UPDATE DASHBOARD
   ============================================================ */

function updateDashboard(fields) {


  console.log(
    "AERIS Firestore fields:",
    fields
  );


  /* ----------------------------------------------------------
     Convert Firestore fields
     ---------------------------------------------------------- */

  const values = {};


  Object.entries(fields).forEach(
    ([key, value]) => {

      values[key] =
        fieldValue(value);

    }
  );


  console.log(
    "AERIS parsed values:",
    values
  );


  /* ==========================================================
     DEBUG TEMPERATURE
     ========================================================== */

  console.log(
    "AERIS temperature raw:",
    fields.temperature
  );


  console.log(
    "AERIS temperature parsed:",
    values.temperature
  );


  /* ==========================================================
     AQI
     ========================================================== */

  const aqi =
    Number(values.aqi) || 0;


  const category =
    classifyAQI(aqi);


  /* ==========================================================
     NORMAL DATA FIELDS
     ========================================================== */

  document
    .querySelectorAll("[data-field]")
    .forEach((element) => {

      const fieldName =
        element.dataset.field;


      const value =
        values[fieldName];


      element.textContent =
        formatValue(value);

    });


  /* ==========================================================
     EXPLICIT TEMPERATURE UPDATE
     
     This is deliberately separate from the generic updater.
     ========================================================== */

  const temperatureElements =
    document.querySelectorAll(
      '[data-field="temperature"]'
    );


  temperatureElements.forEach(
    (element) => {

      const temperature =
        values.temperature;


      if (
        temperature !== null &&
        temperature !== undefined &&
        !Number.isNaN(Number(temperature))
      ) {

        element.textContent =
          Number(temperature).toFixed(1);

      } else {

        element.textContent =
          "--";

      }

    }
  );


  /* ==========================================================
     AQI NUMBER
     ========================================================== */

  setText(
    "[data-aqi]",
    formatValue(aqi, 0)
  );


  /* ==========================================================
     AQI LABEL
     ========================================================== */

  setText(
    "[data-aqi-label]",
    category.label
  );


  /* ==========================================================
     DEVICE STATUS
     ========================================================== */

  setText(
    "[data-status]",
    values.status || "Online"
  );


  setText(
    "[data-status-detail]",
    values.status || "Online"
  );


  /* ==========================================================
     TIMESTAMP
     ========================================================== */

  setText(
    "[data-device-time]",
    values.timestamp
  );


  setText(
    "[data-last-updated]",
    values.timestamp
  );


  /* ==========================================================
     AQI STATUS STYLE
     ========================================================== */

  const status =
    $("[data-aqi-status]");


  if (status) {

    status.style.color =
      category.color;


    status.style.backgroundColor =
      `${category.color}20`;


    const dot =
      status.querySelector(
        ".hero-status-dot"
      );


    if (dot) {

      dot.style.backgroundColor =
        category.color;

    }

  }


  /* ==========================================================
     GAUGE
     ========================================================== */

  renderGauge(
    $("[data-gauge]"),
    aqi
  );

}


/* ============================================================
   LOAD FIRESTORE DEVICE
   ============================================================ */

async function loadDevice() {

  try {

    console.log(
      "AERIS: Fetching Firestore..."
    );


    const response =
      await fetch(
        FIRESTORE_URL,
        {
          cache: "no-store"
        }
      );


    if (!response.ok) {

      throw new Error(
        `Firestore request failed: ${response.status}`
      );

    }


    const documentData =
      await response.json();


    console.log(
      "AERIS Firestore response:",
      documentData
    );


    updateDashboard(
      documentData.fields || {}
    );


  } catch (error) {

    console.error(
      "AERIS: Unable to load Firestore device data",
      error
    );


    setText(
      "[data-status]",
      "Offline"
    );


    setText(
      "[data-status-detail]",
      "Unavailable"
    );

  }

}


/* ============================================================
   LIVE CLOCK
   ============================================================ */

function updateClock() {

  setText(

    "[data-live-clock]",

    new Date().toLocaleTimeString(
      "en-IN",
      {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        hour12: false
      }
    )

  );

}


/* ============================================================
   INITIALIZATION
   ============================================================ */

document.addEventListener(
  "DOMContentLoaded",
  () => {


    /* --------------------------------------------------------
       Clock
       -------------------------------------------------------- */

    updateClock();

    setInterval(
      updateClock,
      1000
    );


    /* --------------------------------------------------------
       Firestore
       -------------------------------------------------------- */

    loadDevice();

    setInterval(
      loadDevice,
      REFRESH_INTERVAL
    );


    /* --------------------------------------------------------
       Navbar
       -------------------------------------------------------- */

    window.addEventListener(

      "scroll",

      () => {

        $("#mainNav")
          ?.classList
          .toggle(
            "is-scrolled",
            window.scrollY > 12
          );

      },

      {
        passive: true
      }

    );

  }
);