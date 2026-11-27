#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <string>
#include <vector>

// Minimal vector PDF writer: one A4 page, base-14 Helvetica, no alpha states.
// Enough for a report card and nothing else - it is deliberately not a
// general-purpose PDF library.
//
// Coordinates are given top-down (y = 0 at the top of the page) because that is
// how a card is laid out; the writer flips them for the PDF coordinate system.
class PdfWriter
{
public:
    static constexpr float pageWidth = 595.28f;   // A4 at 72 dpi
    static constexpr float pageHeight = 841.89f;

    void setFill (juce::Colour colour)
    {
        ops += colourString (colour) + " rg\n";
    }

    void setStroke (juce::Colour colour, float lineWidth = 1.0f)
    {
        ops += colourString (colour) + " RG\n";
        ops += num (lineWidth) + " w\n";
    }

    void fillRect (float x, float y, float w, float h)
    {
        ops += num (x) + " " + num (flipY (y + h)) + " " + num (w) + " " + num (h) + " re f\n";
    }

    void strokeRect (float x, float y, float w, float h)
    {
        ops += num (x) + " " + num (flipY (y + h)) + " " + num (w) + " " + num (h) + " re S\n";
    }

    void line (float x0, float y0, float x1, float y1)
    {
        ops += num (x0) + " " + num (flipY (y0)) + " m "
             + num (x1) + " " + num (flipY (y1)) + " l S\n";
    }

    void dashedLine (float x0, float y0, float x1, float y1)
    {
        ops += "[3 3] 0 d\n";
        line (x0, y0, x1, y1);
        ops += "[] 0 d\n";
    }

    // Polyline through points, used for curves.
    void polyline (const std::vector<juce::Point<float>>& points)
    {
        if (points.size() < 2)
            return;

        ops += num (points.front().x) + " " + num (flipY (points.front().y)) + " m\n";

        for (size_t i = 1; i < points.size(); ++i)
            ops += num (points[i].x) + " " + num (flipY (points[i].y)) + " l\n";

        ops += "S\n";
    }

    enum class Align { left, centre, right };

    void text (float x, float y, const juce::String& value, float size,
               bool bold = false, Align align = Align::left)
    {
        const auto clean = escape (value);

        if (clean.empty())
            return;

        const float w = textWidth (value, size, bold);
        float startX = x;

        if (align == Align::centre)
            startX = x - w * 0.5f;
        else if (align == Align::right)
            startX = x - w;

        ops += "BT /";
        ops += bold ? "F2" : "F1";
        ops += " " + num (size) + " Tf " + num (startX) + " " + num (flipY (y))
             + " Td (" + clean + ") Tj ET\n";
    }

    static float textWidth (const juce::String& value, float size, bool bold)
    {
        // Average Helvetica advance; good enough for laying out a fixed card.
        return (float) value.length() * size * (bold ? 0.56f : 0.52f);
    }

    bool save (const juce::File& file)
    {
        juce::MemoryOutputStream out;

        const std::string header = "%PDF-1.4\n";
        out.write (header.data(), header.size());

        std::vector<long long> offsets;

        auto writeObject = [&] (const std::string& body)
        {
            offsets.push_back ((long long) out.getDataSize());
            out.write (body.data(), body.size());
        };

        writeObject ("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
        writeObject ("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
        writeObject ("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
                     + num (pageWidth) + " " + num (pageHeight)
                     + "] /Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> /Contents 6 0 R >>\nendobj\n");
        writeObject ("4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                     "/Encoding /WinAnsiEncoding >>\nendobj\n");
        writeObject ("5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
                     "/Encoding /WinAnsiEncoding >>\nendobj\n");

        std::string streamObject = "6 0 obj\n<< /Length " + std::to_string (ops.size()) + " >>\nstream\n";
        streamObject += ops;
        streamObject += "endstream\nendobj\n";
        writeObject (streamObject);

        const auto xrefOffset = (long long) out.getDataSize();

        std::string xref = "xref\n0 " + std::to_string (offsets.size() + 1) + "\n";
        xref += "0000000000 65535 f \n";

        char buffer[32];

        for (const auto offset : offsets)
        {
            std::snprintf (buffer, sizeof (buffer), "%010lld 00000 n \n", offset);
            xref += buffer;
        }

        xref += "trailer\n<< /Size " + std::to_string (offsets.size() + 1)
              + " /Root 1 0 R >>\nstartxref\n" + std::to_string (xrefOffset) + "\n%%EOF\n";

        out.write (xref.data(), xref.size());
        out.flush();

        file.deleteFile();

        if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            stream->write (out.getData(), out.getDataSize());
            stream->flush();
            return stream->getStatus().wasOk();
        }

        return false;
    }

private:
    static float flipY (float y) { return pageHeight - y; }

    static std::string num (float value)
    {
        char buffer[32];
        std::snprintf (buffer, sizeof (buffer), "%.2f", value);
        return buffer;
    }

    static std::string colourString (juce::Colour colour)
    {
        char buffer[64];
        std::snprintf (buffer, sizeof (buffer), "%.3f %.3f %.3f",
                       (float) colour.getRed() / 255.0f,
                       (float) colour.getGreen() / 255.0f,
                       (float) colour.getBlue() / 255.0f);
        return buffer;
    }

    // WinAnsi-safe: keep printable ASCII, map the usual typographic characters,
    // drop the rest so a stray non-ASCII byte cannot break the stream.
    static std::string escape (const juce::String& input)
    {
        std::string result;
        result.reserve ((size_t) input.length());

        auto make = [&result] (char c) { result.push_back (c); };

        for (auto character : input)
        {
            switch (character)
            {
                case '(':  make ('\\'); make ('('); break;
                case ')':  make ('\\'); make (')'); break;
                case '\\': make ('\\'); make ('\\'); break;
                case 0x2013: make ('-'); break;
                case 0x2014: make ('-'); break;
                case 0x00D7: make ('x'); break;
                case 0x00B0: make ('o'); break;
                default:
                    make ((character >= 32 && character < 127) ? (char) character : ' ');
                    break;
            }
        }

        return result;
    }

    std::string ops;
};
