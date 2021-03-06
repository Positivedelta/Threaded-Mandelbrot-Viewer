//
// (c) Max van Daalen, August 2020
//

#include <complex>
#include <cstdint>
#include <iostream>
#include <thread>

#include <cairomm/context.h>
#include <gdkmm/general.h>
#include <glibmm/dispatcher.h>
#include <gtkmm/application.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/window.h>

//
// sudo apt install libgtkmm-3.0-dev, libgstreamermm-1.0-dev
// g++ mandelbrot.cpp -std=c++17 -o mandelbrot `pkg-config --cflags --libs gtkmm-3.0`
//

class MandelbrotCanvas : public Gtk::DrawingArea
{
    private:
        const uint32_t detectedCpuCount;
        const int32_t width;
        const int32_t height;
        const Cairo::RefPtr<Cairo::ImageSurface> surface;
        const std::complex<double> initialStart, initialEnd;
        const int32_t defaultMaxIterations;
        int32_t maxIterations;
        uint64_t currentZoom;
        Glib::Dispatcher queueDrawDispatcher;
        std::complex<double> start, centre, end;
        Glib::RefPtr<Gdk::Pixbuf> image;
	guint8* imageData;
        bool busy;

    public:
        MandelbrotCanvas(const std::complex<double> start, const std::complex<double> end, const int32_t defaultMaxIterations, const int32_t width, const int32_t height):
            initialStart(start), initialEnd(end), start(start), centre(start + ((end - start) / 2.0)), end(end), defaultMaxIterations(defaultMaxIterations), maxIterations(defaultMaxIterations),
            queueDrawDispatcher(Glib::Dispatcher()), width(width), height(height), surface(Cairo::ImageSurface::create(Cairo::FORMAT_RGB24, width, height)), busy(false),
            detectedCpuCount(std::thread::hardware_concurrency()), currentZoom(1) {
                image = Gdk::Pixbuf::create_from_data(surface->get_data(), Gdk::COLORSPACE_RGB, true, 8, width, height, surface->get_stride());
                image -> fill(0x000000ff);
                imageData = image -> get_pixels();

                // enable and process left button mouse click events on the canvas
                // uses the screen click location to re-centre the display
                //
                add_events(Gdk::BUTTON_PRESS_MASK);
                signal_button_press_event().connect([this](GdkEventButton* event) {
                    if ((event->button == 1) && !busy)
                    {
                        busy = true;
                        reCentre(event->x, event->y);
                        return true;
                    }

                    return false;
                });

                // the UI events are all handled in threads that invoke the GPU and once complete call queue_draw(), however this can
                // only be called from the thread that initialised Gtk and is achieved using a Glibmm dispatcher
                //
                queueDrawDispatcher.connect([this]() {
                    queue_draw();
                });

                // generate the fractal
		//
                std::cout << "Number of detected CPUs: " << detectedCpuCount << " (will allocate this many rendering threads)\n";
                drawMandelbrot();
        };

        virtual ~MandelbrotCanvas()
        {
            //
            // nothing to do...
            //
        };

        void reset()
        {
            if (busy) return;

            std::cout << "Resetting the mandelbrot render, centred between: " << initialStart << ", " << initialEnd << "\n";
            start = initialStart;
            centre = initialStart + ((initialEnd - initialStart) / 2.0);
            end = initialEnd;
            maxIterations = defaultMaxIterations;
            currentZoom = 1;
            busy = true;
            auto resetCpu = std::thread([=]() {
                drawMandelbrot();
                queueDrawDispatcher.emit();
                busy = false;
            });

            resetCpu.detach();
        }

        void refresh(int32_t updatedMaxIterations)
        {
            if (busy) return;

            std::cout << "Refreshing the mandelbrot render, centred between: " << start << ", " << end << ", max iterations: " << updatedMaxIterations << "\n";
            maxIterations = updatedMaxIterations;
            busy = true;
            auto refreshCpu = std::thread([=]() {
                drawMandelbrot();
                queueDrawDispatcher.emit();
                busy = false;
            });

            refreshCpu.detach();
        }

        // note, always generates a fully zoomed out view
        //
        void renderSpecificCoordinate(double real, double imag, int32_t updatedMaxIterations)
        {
            if (busy) return;

            std::cout << "Render specific coordinate, real: " << real << ", imaginary: " << imag << ", max iterations: " << updatedMaxIterations << "\n";
            maxIterations = updatedMaxIterations;
            busy = true;
            auto coordCpu = std::thread([=]() {
                auto rangeAdjustment = (initialEnd - initialStart) / 2.0;
                start = std::complex<double>(real - rangeAdjustment.real(), rangeAdjustment.imag() + imag);
                centre = std::complex<double>(real, imag);
                end = std::complex<double>(rangeAdjustment.real() + real, imag - rangeAdjustment.imag());
                drawMandelbrot();
                queueDrawDispatcher.emit();
                busy = false;
            });

            coordCpu.detach();
        }

        void centreZoom(int32_t updatedMaxIterations)
        {
            if (busy) return;

            busy = true;
            maxIterations = updatedMaxIterations;
            currentZoom = currentZoom << 1;
            auto centreZoomCpu = std::thread([=]() {
                std::cout << "Zoom initial start: " << start << ", centre: " << centre << ", end: " << end << "\n";

                const auto zoomRange = (end - start) / 4.0;
                start = centre - zoomRange;
                end = centre + zoomRange;
                std::cout << "Zoomed updated start: " << start << ", centre: " << centre << ", end: " << end << "\n";
                std::cout << "Current zoom factor: x" << currentZoom << "\n";

                drawMandelbrot();
                queueDrawDispatcher.emit();
                busy = false;
            });

            centreZoomCpu.detach();
        }

    protected:
        bool on_draw(const Cairo::RefPtr<Cairo::Context>& context) override
        {
            Gdk::Cairo::set_source_pixbuf(context, image, 0.0, 0.0);
            context -> paint();

/*          // note, this code has been left here to illustrate how to draw over the above mandelbrot render
            //
            context -> set_line_width(2.0);
            context -> set_source_rgb(0.8, 0.0, 0.0);
            context -> move_to(0, height >> 1);
            context -> line_to(width, height >> 1);
            context -> stroke(); */

            return true;
        }

    private:
        void reCentre(double x, double y)
        {
            auto reCentreCpu = std::thread([=]() {
                std::cout << "Redraw at (" << x << ", " << y << ")\n";
                std::cout << "Click to re-centre initial start: " << start << ", centre: " << centre << ", end: " << end << "\n";

                const auto range = end - start;
                const auto clickCentre = std::complex<double>(start.real() + (range.real() * x / (double)width), start.imag() + (range.imag() * y / (double)height));
                std::cout << "Range: " << range << "\n";
                std::cout << "Click location: " << clickCentre << "\n";

                start = std::complex<double>(clickCentre.real() - (range.real() / 2.0), clickCentre.imag() - (range.imag() / 2.0));
                end = std::complex<double>(clickCentre.real() + (range.real() / 2.0), clickCentre.imag() + (range.imag() / 2.0));
                centre = start + ((end - start) / 2.0);
                std::cout << "Click to re-centre updated start: " << start << "centre: " << centre << ", end: " << end << "\n";

                drawMandelbrot();
                queueDrawDispatcher.emit();
                busy = false;
            });

            reCentreCpu.detach();
        }

/*      // use for periodic B/W rendering
        // FIXME! remove 'magnitude' if it's not going to get used
        //
        void setRGB(const int32_t x, const int32_t y, const double magnitude, const int32_t iterations)
        {
            constexpr double twoPi = 2.0 * M_PI;
            const double colour = (double)iterations / (double)maxIterations;
            //const double colour = log(magnitude / (1.0 - (double)iterations / (double)maxIterations));
            const double omega = (1.0 + cos(twoPi * colour)) / 2.0;
            const uint8_t rgb = (uint8_t)(255.0 * omega);

            guint8 *pixel = &imageData[(y * image->get_rowstride()) + (x * image->get_n_channels())];
	    pixel[0] = rgb;
	    pixel[1] = rgb;
	    pixel[2] = rgb;
        } */

        // FIXME! remove 'magnitude' if it's not going to get used
        //
        void setRGB(const int32_t x, const int32_t y, const double magnitude, const int32_t iterations)
        {
            double colour = (double)iterations / (double)maxIterations;

//          // for psychedelic colouring use, but a bit noisey in the usual areas of interest
//          //
//          constexpr double twoPi = 2.0 * M_PI;
//          //const double omega = log(magnitude / ((double)maxIterations / (double)iterations));
//          const double omega = log(magnitude / (double)iterations);
//          const double colour = (1.0 + cos(twoPi * omega)) / 2.0;

            // use a smooth bernstein polynomial to generate RGB
            //
            uint8_t r = (uint8_t)(9 * (1 - colour) * colour * colour * colour * 255);
            uint8_t g = (uint8_t)(15 * (1 - colour) * (1 - colour) * colour * colour * 255);
            uint8_t b =  (uint8_t)(8.5 * (1 - colour) * (1 - colour) * (1 - colour) * colour * 255);

            guint8 *pixel = &imageData[(y * image->get_rowstride()) + (x * image->get_n_channels())];
	    pixel[0] = r;
	    pixel[1] = g;
	    pixel[2] = b;
        }

        // takes advantage of all available CPU cores, allocation is forced as one thread per core
        //
        void drawMandelbrot()
        {
            std::cout << "Start rendering...\n";

            auto base = start;
            const auto range = end - start;
            const auto xStep = std::complex<double>(range.real() / (double)width, 0.0);
            const auto yOuterStep = std::complex<double>(0.0, (range.imag() / (double)detectedCpuCount));

            int32_t yStart = 0;
            const int32_t yIncrement = height / detectedCpuCount;
            const auto yInnerStep = std::complex<double>(0.0, yOuterStep.imag() / (double)yIncrement);

            std::cout << "Launching threads ";
            auto renderingThreads = std::vector<std::thread>(detectedCpuCount);
            for (int32_t cpu = 0; cpu < detectedCpuCount; cpu++)
            {
                std::cout << "#" << cpu << " ";
                renderingThreads[cpu] = std::thread([this, base, xStep, yInnerStep, yStart, yIncrement, cpu]() {
                    auto innerBase = base;
                    for (int32_t y = yStart; y < (yStart + yIncrement); y++)
                    {
                        auto c = innerBase;
                        for (int32_t x = 0; x < width; x++)
                        {
                            auto z = std::complex<double>(0.0, 0.0);
                            int32_t iterations = maxIterations;
                            while ((norm(z) < 4.0) && (iterations > 0))
                            {
                                z = (z * z) + c;
                                iterations--;
                            }

                            setRGB(x, y, abs(z), iterations);
                            c = c + xStep;
                        }

                        innerBase = innerBase + yInnerStep;
                    }
                });

                // a thread can be assigned to a set of CPUs, for each rendering thread create a set containing a single unique CPU
                //
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu, &cpuset);
                const int32_t setStatus = pthread_setaffinity_np(renderingThreads[cpu].native_handle(), sizeof(cpu_set_t), &cpuset);
                if (setStatus != 0) std::cout << "\n\nWARNING -- Failed to assign a unique CPU to thread #" << cpu << " (normal allocation OS should occur)\n\n";

                base = base + yOuterStep;
                yStart = yStart + yIncrement;
            }

            std::cout << "\n";

            // wait for all of the rendering threads to complete
            //
            for (auto& cpu : renderingThreads) cpu.join();
            std::cout << "Finished rendering\n\n";
        }
};

class Mandelbrot : public Gtk::Window
{
    private:
        const int32_t BUTTON_WIDTH = 80;
        const int32_t BUTTON_HEIGHT = 32;
        const int32_t CONTROL_PADDING = 5;
        const int32_t HBOX_HEIGHT = BUTTON_HEIGHT + (CONTROL_PADDING << 1);
        Gtk::VBox verticalBox;
        Gtk::HBox horizontalBox;
        Gtk::Button resetButton, refreshButton, applyButton, zoomButton;
        Gtk::Label iterationLabel, coordLabel;
        Gtk::Entry realCoord, imagCoord;
        Gtk::SpinButton iterationEntry;
        MandelbrotCanvas mandelbrotCanvas;

    public:
        Mandelbrot(const std::complex<double> start, std::complex<double> end, const int32_t defaultMaxIterations, const int32_t width, const int32_t height):
            mandelbrotCanvas(start, end, defaultMaxIterations, width, height), resetButton("Reset"), refreshButton("Refresh"), iterationLabel("Iterations:"), iterationEntry(),
            coordLabel("Coordinate:"), realCoord(), imagCoord(), applyButton("Apply"), zoomButton("Zoom") {
                set_resizable(false);
                set_size_request(width, height + HBOX_HEIGHT);
                set_position(Gtk::WIN_POS_CENTER);
                set_title("GTK+3.0 Mandelbrot (Multi Core Version)");

                // setup the iterations text field
                //
                iterationEntry.set_digits(0);                   // show integer values only, i.e. 0 decimal places
                iterationEntry.set_numeric(true);
                iterationEntry.set_max_length(9);
                iterationEntry.set_width_chars(9);              // sets the preferred spinner length
                iterationEntry.set_range(10.0, 999999999.0);
                iterationEntry.set_increments(100.0, 1000.0);
                iterationEntry.set_value(defaultMaxIterations);

                // this button re-paints the render, usually invoked after the iteration limit is increased
                //
                refreshButton.set_size_request(BUTTON_WIDTH, BUTTON_HEIGHT);
                refreshButton.signal_clicked().connect([this]() {
                    mandelbrotCanvas.refresh(iterationEntry.get_value_as_int());
                });

                // specific coordinate entry fields
                //
                realCoord.set_placeholder_text("real component");
                imagCoord.set_placeholder_text("imaginary component");
                applyButton.set_size_request(BUTTON_WIDTH, BUTTON_HEIGHT);
                applyButton.signal_clicked().connect([this]() {
                    if ((realCoord.get_text_length() == 0) || (imagCoord.get_text_length() == 0)) return;

                    try
                    {
                        // FIXME! stod does not check for garbage at the end of the string
                        //
                        const double real = std::stod(realCoord.get_text(), NULL);
                        const double imag = std::stod(imagCoord.get_text(), NULL);
                        mandelbrotCanvas.renderSpecificCoordinate(real, imag, iterationEntry.get_value_as_int());
                    }
                    catch (...)
                    {
                        realCoord.set_text("");
                        imagCoord.set_text("");
                        std::cout << "Invalid coordinate entered!\n";
                    }
                });

                zoomButton.set_size_request(BUTTON_WIDTH, BUTTON_HEIGHT);
                zoomButton.signal_clicked().connect([this]() {
                    mandelbrotCanvas.centreZoom(iterationEntry.get_value_as_int());
                });

                // this button resets the mandelbrot render
                //
                resetButton.set_size_request(BUTTON_WIDTH, BUTTON_HEIGHT);
                resetButton.signal_clicked().connect([=]() {
                    iterationEntry.set_value(defaultMaxIterations);
                    realCoord.set_text("");
                    imagCoord.set_text("");
                    mandelbrotCanvas.reset();
                });

                // note 1, the refresh button is given the default focus
                //      2, the specific coordinate label is 'expanded' outwards and right aligned
                //      3, the zoom button is 'expanded' outwards and left aligned
                //
                verticalBox.pack_start(mandelbrotCanvas);
                horizontalBox.pack_start(iterationLabel, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                horizontalBox.pack_start(iterationEntry, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                horizontalBox.pack_start(refreshButton, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                horizontalBox.pack_start(zoomButton, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);

                coordLabel.set_xalign(1.0);
                horizontalBox.pack_start(coordLabel, Gtk::PackOptions::PACK_EXPAND_WIDGET, CONTROL_PADDING);
                horizontalBox.pack_start(realCoord, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                horizontalBox.pack_start(imagCoord, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                applyButton.set_halign(Gtk::Align::ALIGN_START);
                horizontalBox.pack_start(applyButton, Gtk::PackOptions::PACK_EXPAND_WIDGET, CONTROL_PADDING);

                horizontalBox.pack_end(resetButton, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                horizontalBox.set_focus_child(refreshButton);
                verticalBox.pack_start(horizontalBox, Gtk::PackOptions::PACK_SHRINK, CONTROL_PADDING);
                add(verticalBox);
                show_all_children();
        }

        ~Mandelbrot()
        {
            //
            // nothing to do...
            //
        }
};

int32_t main(int32_t argc, char *argv[])
{
    const auto application = Gtk::Application::create(argc, argv, "bitparallel.com.gtkmm.mandelbrot.application");
    std::cout << "Mandelbrot plot GTK+3.0 application UI (multi core version)\n";

    // the width has been adjusted to match the value used in the GPU version
    //
    const int32_t renderWidth = 1216;           // 600, 1200
    const int32_t renderHeight = 800;           // 400, 800
    const int32_t defaultMaxIterations = 100;   // 1000

/*  const double xCentre = 0.2929859127507;
    const double yCentre = 0.6117848324958;
    const double radius = 4.4e-11 / 16.0; */

    // modified for asthetics, the actual fractal limits are (-2.0, 1.0), (1.0, -1.0)
    // but must still maintain the correct 3:2 aspect ratio
    //
    const auto initialStart = std::complex<double>(-2.28, 1.14);
    const auto initialEnd = std::complex<double>(1.14, -1.14);
    std::cout << "Start: " << initialStart << ", end: " << initialEnd << "\n";

    const auto displayAspectRatio = (double)renderWidth / (double)renderHeight;
    const auto initialCoordinateRange = initialEnd - initialStart;
    const auto initialCoordinateAspectRatio = abs(initialCoordinateRange.real() / initialCoordinateRange.imag());
    std::cout << "Screen aspect ratio: " << displayAspectRatio << "\n";
    std::cout << "Coordinate aspect ratio: " << initialCoordinateAspectRatio << "\n";

    auto start = initialStart;
    auto end = initialEnd;
    if (displayAspectRatio != initialCoordinateAspectRatio)
    {
        const auto correction = displayAspectRatio / initialCoordinateAspectRatio;
        start = std::complex<double>(initialStart.real() * correction, initialStart.imag());
        end = std::complex<double>(initialEnd.real() * correction, initialEnd.imag());
        const auto correctedRange = end - start;
        std::cout << "Corrected start: " << start << ", end: " << end << "\n";
        std::cout << "Corrected coordinate aspect ratio: " << abs(correctedRange.real() / correctedRange.imag()) << "\n";
    }

    Mandelbrot mandelbrot = Mandelbrot(start, end, defaultMaxIterations, renderWidth, renderHeight);
    return application -> run(mandelbrot);
}
