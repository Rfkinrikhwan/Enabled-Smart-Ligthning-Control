document.addEventListener("DOMContentLoaded", function () {
  const lightsContainer = document.getElementById("lights-container");
  const activeCount = document.getElementById("active-count");
  const totalLightsSpan = document.getElementById("total-lights");
  const energyUsage = document.getElementById("energy-usage");
  const systemStatus = document.getElementById("system-status");
  const systemStatusDetail = document.getElementById("system-status-detail");
  const allOnButton = document.getElementById("all-on");
  const allOffButton = document.getElementById("all-off");
  const runningLedButton = document.getElementById("running-led");

  // New elements for IP address
  const ipAddressInput = document.getElementById("ip-address-input");
  const saveIpButton = document.getElementById("save-ip-button");

  // Get IP address from localStorage or use default
  let currentIpAddress =
    localStorage.getItem("lightingIpAddress") || "192.168.117.1";
  ipAddressInput.value = currentIpAddress;

  // Save IP address to localStorage
  saveIpButton.addEventListener("click", function () {
    const newIpAddress = ipAddressInput.value.trim();
    if (newIpAddress) {
      localStorage.setItem("lightingIpAddress", newIpAddress);
      currentIpAddress = newIpAddress;
      fetchLampStatus(); // Refresh with new IP
      alert("IP address saved successfully!");
    } else {
      alert("Please enter a valid IP address");
    }
  });

  // Track running LED state
  let isRunningLedActive = false;

  // Function to create a single light card
  function createLightCard(lamp) {
    const card = document.createElement("div");
    card.className = `neumorphic-inset p-6 ${
      lamp.status === "ON" ? "light-on" : "light-off"
    }`;
    card.innerHTML = `
      <div class="flex justify-between items-center mb-6">
        <div class="flex items-center">
          <div class="light-icon-container neumorphic-inset w-14 h-14 rounded-full flex items-center justify-center mr-4">
            <i class="light-icon fa-solid fa-lightbulb text-3xl ${
              lamp.status === "ON" ? "text-yellow-400" : "text-gray-400"
            }"></i>
          </div>
          <div>
            <h2 class="text-xl font-semibold text-slate-800">${`Light ${
              lamp.id + 1
            }`}</h2>
          </div>
        </div>
        <div class="flex items-center">
          <span class="status-badge ${
            lamp.status === "ON" ? "status-on" : "status-off"
          } mr-3">${lamp.status}</span>
          <label class="switch-3d">
            <input type="checkbox" class="light-toggle-checkbox" data-light="${
              lamp.id
            }" ${lamp.status === "ON" ? "checked" : ""} />
            <span class="slider-3d"></span>
          </label>
        </div>
      </div>
      <div class="flex justify-between items-center mt-6">
        <div class="flex items-center space-x-4">
          <input 
            type="color" 
            class="color-picker" 
            data-light="${lamp.id}" 
            value="#${rgbToHex(
              lamp.currentColor.r,
              lamp.currentColor.g,
              lamp.currentColor.b
            )}"
            ${lamp.status === "ON" ? "" : "disabled"}
          />
          <div class="text-sm text-slate-500">
            <span class="current-color-label">Color:</span>
            <span class="current-color-value">#${rgbToHex(
              lamp.currentColor.r,
              lamp.currentColor.g,
              lamp.currentColor.b
            )}</span>
          </div>
        </div>
      </div>
    `;
    return card;
  }

  // Convert RGB to Hex
  function rgbToHex(r, g, b) {
    return [r, g, b]
      .map((x) => {
        const hex = x.toString(16);
        return hex.length === 1 ? "0" + hex : hex;
      })
      .join("");
  }

  // Convert Hex to RGB
  function hexToRgb(hex) {
    const bigint = parseInt(hex.slice(1), 16);
    const r = (bigint >> 16) & 255;
    const g = (bigint >> 8) & 255;
    const b = bigint & 255;
    return { r, g, b };
  }

  // Fetch lamp status
  async function fetchLampStatus() {
    try {
      const response = await fetch(`http://${currentIpAddress}/lamp/status`, {
        headers: {
          "Content-Type": "application/json",
        },
      });
      const data = await response.json();

      // Update running LED button state if available in response
      if (data.hasOwnProperty("runningLedActive")) {
        isRunningLedActive = data.runningLedActive;
        updateRunningLedButtonAppearance();
      }

      // Clear existing lights
      lightsContainer.innerHTML = "";

      // Create light cards
      data.lamps.forEach((lamp) => {
        const lightCard = createLightCard(lamp);
        lightsContainer.appendChild(lightCard);
      });

      // Update active lights count
      const activeLights = data.lamps.filter(
        (lamp) => lamp.status === "ON"
      ).length;
      activeCount.textContent = activeLights;
      totalLightsSpan.textContent = data.lamps.length;

      // Update energy usage (assuming 0.06 kWh per active light)
      energyUsage.textContent = (activeLights * 0.06).toFixed(2) + " kWh";

      // Update system status
      systemStatus.textContent = "Online";
      systemStatus.classList.remove("text-red-500");
      systemStatus.classList.add("text-emerald-500");
      systemStatusDetail.textContent = "All systems operational";

      // Attach event listeners
      attachLightControlListeners();
    } catch (error) {
      console.error("Error fetching lamp status:", error);
      systemStatus.textContent = "Offline";
      systemStatus.classList.remove("text-emerald-500");
      systemStatus.classList.add("text-red-500");
      systemStatusDetail.textContent = "Connection error";
    }
  }

  // Update the Running LED button appearance based on its state
  function updateRunningLedButtonAppearance() {
    if (isRunningLedActive) {
      runningLedButton.classList.remove(
        "bg-emerald-500",
        "hover:bg-emerald-600"
      );
      runningLedButton.classList.add("bg-orange-500", "hover:bg-orange-600");
      runningLedButton.innerHTML =
        '<i class="fa-solid fa-stop mr-2"></i> Stop Running';
    } else {
      runningLedButton.classList.remove("bg-orange-500", "hover:bg-orange-600");
      runningLedButton.classList.add("bg-emerald-500", "hover:bg-emerald-600");
      runningLedButton.innerHTML =
        '<i class="fa-solid fa-play mr-2"></i> Running Led';
    }
  }

  // Toggle running LED mode
  async function toggleRunningLed() {
    try {
      // Get the current color from the first active light or use default red
      let color = {
        r: Math.floor(Math.random() * 256),
        g: Math.floor(Math.random() * 256),
        b: Math.floor(Math.random() * 256),
      }; // Random color
      const activeColorPicker = document.querySelector(
        ".light-on .color-picker"
      );
      if (activeColorPicker) {
        color = hexToRgb(activeColorPicker.value);
      }

      const response = await fetch(`http://${currentIpAddress}/lamp/running`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          enable: !isRunningLedActive,
          color: color,
          interval: 200, // Default interval of 200ms
        }),
      });

      if (response.ok) {
        isRunningLedActive = !isRunningLedActive;
        updateRunningLedButtonAppearance();
        fetchLampStatus(); // Refresh the status
      } else {
        const errorData = await response.json();
        alert(errorData.error || "Failed to toggle running LED mode");
      }
    } catch (error) {
      console.error("Error toggling running LED mode:", error);
      alert("Connection error");
    }
  }

  // Attach event listeners to light controls
  function attachLightControlListeners() {
    // Light toggle buttons
    document
      .querySelectorAll(".light-toggle, .light-toggle-checkbox")
      .forEach((toggle) => {
        toggle.addEventListener("click", async function () {
          const lightId = this.getAttribute("data-light");
          const card = this.closest(".neumorphic-inset");
          const isCurrentlyOn = card.classList.contains("light-on");

          try {
            const endpoint = isCurrentlyOn
              ? `http://${currentIpAddress}/lamp/off`
              : `http://${currentIpAddress}/lamp/on`;
            const response = await fetch(endpoint, {
              method: "POST",
              headers: {
                "Content-Type": "application/json",
              },
              body: JSON.stringify({ id: parseInt(lightId) }),
            });

            if (response.ok) {
              // Refresh lamp status
              fetchLampStatus();
            } else {
              const errorData = await response.json();
              alert(errorData.error || "Failed to toggle light");
            }
          } catch (error) {
            console.error("Error toggling light:", error);
            alert("Connection error");
          }
        });
      });

    // Color picker listeners
    document.querySelectorAll(".color-picker").forEach((colorPicker) => {
      colorPicker.addEventListener("change", async function () {
        const lightId = this.getAttribute("data-light");
        const color = hexToRgb(this.value);

        try {
          const response = await fetch(
            `http://${currentIpAddress}/lamp/color`,
            {
              method: "POST",
              headers: {
                "Content-Type": "application/json",
              },
              body: JSON.stringify({
                id: parseInt(lightId),
                color: color,
              }),
            }
          );

          if (response.ok) {
            // Optionally refresh status or update UI
            fetchLampStatus();
          } else {
            const errorData = await response.json();
            alert(errorData.error || "Failed to change color");
          }
        } catch (error) {
          console.error("Error changing light color:", error);
          alert("Connection error");
        }
      });
    });

    // All on/off buttons
    allOnButton.addEventListener("click", async function () {
      try {
        const response = await fetch(`http://${currentIpAddress}/lamp/all/on`);
        if (response.ok) {
          fetchLampStatus();
        } else {
          const errorData = await response.json();
          alert(errorData.error || "Failed to turn all lights on");
        }
      } catch (error) {
        console.error("Error turning all lights on:", error);
        alert("Connection error");
      }
    });

    allOffButton.addEventListener("click", async function () {
      try {
        const response = await fetch(`http://${currentIpAddress}/lamp/all/off`);
        if (response.ok) {
          fetchLampStatus();
        } else {
          const errorData = await response.json();
          alert(errorData.error || "Failed to turn all lights off");
        }
      } catch (error) {
        console.error("Error turning all lights off:", error);
        alert("Connection error");
      }
    });

    // Running LED button
    runningLedButton.addEventListener("click", toggleRunningLed);
  }

  // Initial fetch of lamp status
  fetchLampStatus();

  // Optional: Periodically refresh lamp status
  setInterval(fetchLampStatus, 30000); // Refresh every 30 seconds

  // 3D tilt effect for cards (previous implementation remains the same)
  const cards = document.querySelectorAll(".card-3d");
  cards.forEach((card) => {
    card.addEventListener("mousemove", function (e) {
      const rect = this.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;

      const centerX = rect.width / 2;
      const centerY = rect.height / 2;

      const rotateX = (y - centerY) / 20;
      const rotateY = (centerX - x) / 20;

      this.style.transform = `perspective(1000px) rotateX(${rotateX}deg) rotateY(${rotateY}deg) translateZ(10px)`;
    });

    card.addEventListener("mouseleave", function () {
      this.style.transform =
        "perspective(1000px) rotateX(0) rotateY(0) translateZ(0)";
      setTimeout(() => {
        this.style.transform = "";
      }, 500);
    });
  });
});
