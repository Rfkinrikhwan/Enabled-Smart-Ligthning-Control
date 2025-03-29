// Initialize light states (all off by default)
const lightStates = {
  1: false,
  2: false,
  3: false,
  4: false,
  5: false,
  6: false,
};

// Track last toggled time
const lastToggled = {
  1: null,
  2: null,
  3: null,
  4: null,
  5: null,
  6: null,
};

// Get all toggle buttons and checkboxes
const toggleButtons = document.querySelectorAll(".light-toggle");
const toggleCheckboxes = document.querySelectorAll(".light-toggle-checkbox");

// Add event listeners to each toggle button
toggleButtons.forEach((button) => {
  button.addEventListener("click", function () {
    const lightId = this.getAttribute("data-light");
    toggleLight(lightId);
  });
});

// Add event listeners to each toggle checkbox
toggleCheckboxes.forEach((checkbox) => {
  checkbox.addEventListener("change", function () {
    const lightId = this.getAttribute("data-light");
    toggleLight(lightId);
  });
});

// Add event listeners for master controls
document.getElementById("all-on").addEventListener("click", function () {
  setAllLights(true);
});

document.getElementById("all-off").addEventListener("click", function () {
  setAllLights(false);
});

// Function to toggle individual light
function toggleLight(lightId) {
  lightStates[lightId] = !lightStates[lightId];
  lastToggled[lightId] = new Date();
  updateLightUI(lightId);
  updateDashboardStats();
}

// Function to set all lights to a specific state
function setAllLights(state) {
  const now = new Date();
  for (let lightId in lightStates) {
    lightStates[lightId] = state;
    lastToggled[lightId] = now;
    updateLightUI(lightId);
  }
  updateDashboardStats();
}

// Function to update the UI for a specific light
function updateLightUI(lightId) {
  const lightCard = document
    .querySelector(`button[data-light="${lightId}"]`)
    .closest(".light-card");
  const statusText = lightCard.querySelector(".status-text");
  const toggleButton = lightCard.querySelector(".light-toggle");
  const lightIcon = lightCard.querySelector(".light-icon");
  const checkbox = lightCard.querySelector(".light-toggle-checkbox");
  const lastToggledText = lightCard.querySelector(".text-sm.text-slate-500");

  if (lightStates[lightId]) {
    // Light is ON
    lightCard.classList.remove("light-off");
    lightCard.classList.add("light-on");
    statusText.classList.remove("text-slate-500");
    statusText.classList.add("text-green-500");
    statusText.textContent = "ON";
    toggleButton.textContent = "Toggle";
    checkbox.checked = true;

    // Update light icon
    lightIcon.classList.remove("text-gray-400");
    lightIcon.classList.add("text-yellow-400", "glow");
  } else {
    // Light is OFF
    lightCard.classList.remove("light-on");
    lightCard.classList.add("light-off");
    statusText.classList.remove("text-green-500");
    statusText.classList.add("text-slate-500");
    statusText.textContent = "OFF";
    toggleButton.textContent = "Toggle";
    checkbox.checked = false;

    // Update light icon
    lightIcon.classList.remove("text-yellow-400", "glow");
    lightIcon.classList.add("text-gray-400");
  }

  // Update last toggled time
  if (lastToggled[lightId]) {
    const timeString = lastToggled[lightId].toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
    });
    lastToggledText.innerHTML = `<i class="fa-solid fa-clock mr-1"></i> Last toggled: ${timeString}`;
  }
}

// Function to update dashboard statistics
function updateDashboardStats() {
  // Count active lights
  const activeCount = Object.values(lightStates).filter(
    (state) => state
  ).length;
  document.getElementById("active-count").textContent = activeCount;

  // Calculate estimated energy usage (0.06 kWh per light per hour)
  const energyUsage = (activeCount * 0.06).toFixed(2);
  document.getElementById("energy-usage").textContent = `${energyUsage} kWh`;
}

// Initialize dashboard stats
updateDashboardStats();
