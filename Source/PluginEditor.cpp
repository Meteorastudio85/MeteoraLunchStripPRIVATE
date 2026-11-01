#include "PluginEditor.h"
#include "GuiLayout.h"
#include "ChainOrder.h"
#include <BinaryData.h>

using namespace juce;

namespace
{

Image loadImage(const String& name)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource(name.toRawUTF8(), size))
        return ImageCache::getFromMemory(data, size);
    jassertfalse;   // asset mancante in Assets/
    return {};
}

//==============================================================================
// Manopola: disegna la texture reale ruotata (l'indicatore stampato segue il valore).
class KnobSlider : public Slider
{
public:
    KnobSlider(Image t, Image c, float baseDeg_) : tex(std::move(t)), cap(std::move(c)), baseDeg(baseDeg_)
    {
        setSliderStyle(RotaryVerticalDrag);
        setTextBoxStyle(NoTextBox, true, 0, 0);
        setMouseDragSensitivity(260);
    }

    std::function<void()> onRightClick;

    // corsa della manopola in gradi (0 = in alto, senso orario); di default -135..+135
    void setRotaryRange(float startDegrees, float endDegrees) { startDeg = startDegrees; endDeg = endDegrees; }

    // manopola a scatti: l'angolo di ogni posizione (indice 0..n-1) e' quello stampato sul pannello
    void setStepAngles(std::vector<float> a) { stepAngles = std::move(a); }

    void paint(Graphics& g) override
    {
        if (! tex.isValid()) return;
        g.setImageResamplingQuality(Graphics::highResamplingQuality);

        const float pos = (float) valueToProportionOfLength(getValue());
        float deg = startDeg + (endDeg - startDeg) * pos;
        if (! stepAngles.empty())
            deg = stepAngles[(size_t) jlimit(0, (int) stepAngles.size() - 1, roundToInt((float) getValue()))];
        const float rad = degreesToRadians(deg - baseDeg);
        const auto  b   = getLocalBounds().toFloat();
        const float s   = b.getWidth() / (float) tex.getWidth();

        g.drawImageTransformed(tex, AffineTransform::translation(-tex.getWidth() * 0.5f, -tex.getHeight() * 0.5f)
                                        .rotated(rad).scaled(s).translated(b.getCentreX(), b.getCentreY()));
        if (cap.isValid())
        {
            // il tappo con il logo e' ad alta risoluzione (656 px): occupa 164 px della texture della ghiera
            const float cs = s * kCapDesignSize / (float) cap.getWidth();
            g.drawImageTransformed(cap, AffineTransform::translation(-cap.getWidth() * 0.5f, -cap.getHeight() * 0.5f)
                                            .scaled(cs).translated(b.getCentreX(), b.getCentreY()));
        }
    }

    void mouseDown(const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        Slider::mouseDown(e);
    }

private:
    static constexpr float kCapDesignSize = 128.0f;   // diametro del logo fermo al centro, in pixel della texture della ghiera (200 px)
    Image tex, cap;
    float baseDeg;
    float startDeg = -135.0f, endDeg = 135.0f;
    std::vector<float> stepAngles;
};

//==============================================================================
// Pulsante trasparente con indicatore disegnato sopra l'immagine.
class HotButton : public Button
{
public:
    enum class Kind { DimWhenOff, Dot, Ring, Pill, Ratio, Sprite };
    Image spriteOn, spriteOff;   // Kind::Sprite: foto del pulsante acceso e spento

    HotButton(Kind k, const String& text, float designW_, float designH_)
        : Button(text), kind(k), label(text), dw(designW_), dh(designH_)
    {
        setClickingTogglesState(true);
    }

    Colour onColour  { 0xffffb030 };
    Colour offColour { 0xff1a1411 };
    Point<float> dotOffset { 44.0f, 0.0f };
    bool engraved = false;   // pillola sulla barra metallica

    void paintButton(Graphics& g, bool over, bool down) override
    {
        const float s  = (float) getWidth() / dw;
        const auto  b  = getLocalBounds().toFloat();
        const bool  on = getToggleState();

        switch (kind)
        {
            case Kind::DimWhenOff:
                if (! on)   // copre il LED acceso stampato nell'immagine
                {
                    g.setColour(offColour.withAlpha(0.96f));
                    g.fillEllipse(b.reduced(b.getWidth() * 0.12f));
                    g.setColour(Colours::white.withAlpha(0.08f));
                    g.fillEllipse(b.getCentreX() - 3 * s, b.getCentreY() - 6 * s, 6 * s, 4 * s);
                }
                break;

            case Kind::Dot:
            {
                const auto c = b.getCentre() + dotOffset * s;
                const float r = 7.0f * s;
                if (on)
                {
                    g.setGradientFill(ColourGradient(onColour.withAlpha(0.55f), c.x, c.y, onColour.withAlpha(0.0f), c.x + 20 * s, c.y, true));
                    g.fillEllipse(c.x - 20 * s, c.y - 20 * s, 40 * s, 40 * s);
                    g.setColour(onColour.brighter(0.4f));
                }
                else g.setColour(Colour(0xff2a2420));
                g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
                g.setColour(Colours::black.withAlpha(0.7f));
                g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.5f * s);
                break;
            }

            case Kind::Ring:
                if (on)
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        g.setColour(onColour.withAlpha(0.85f - 0.28f * i));
                        g.drawEllipse(b.reduced((2.0f + 3.0f * i) * s), (3.0f + 2.0f * i) * s);
                    }
                }
                break;

            case Kind::Pill:
            {
                const auto r = b.reduced(0.5f);
                const float cr = r.getHeight() * 0.35f;
                if (engraved)
                {
                    g.setColour(on ? onColour.withAlpha(0.85f) : Colours::black.withAlpha(0.18f));
                    g.fillRoundedRectangle(r, cr);
                    g.setColour(Colours::black.withAlpha(0.55f));
                    g.drawRoundedRectangle(r, cr, 1.5f * s);
                    g.setColour(on ? Colour(0xff201305) : Colour(0xff2b2620));
                }
                else
                {
                    g.setColour(on ? onColour.withAlpha(0.92f) : Colour(0xff1b1815).withAlpha(0.88f));
                    g.fillRoundedRectangle(r, cr);
                    g.setColour(Colour(0xff6b6258));
                    g.drawRoundedRectangle(r, cr, 1.2f * s);
                    g.setColour(on ? Colour(0xff201305) : Colour(0xffcfc6b8));
                }
                g.setFont(Font(FontOptions(dh * 0.52f * s, Font::bold)));
                g.drawText(label, b, Justification::centred);
                break;
            }

            case Kind::Sprite:
            {
                const auto& im = on ? spriteOn : spriteOff;
                if (im.isValid())
                {
                    g.setImageResamplingQuality(Graphics::highResamplingQuality);
                    g.drawImage(im, b);
                }
                if (down) { g.setColour(Colours::black.withAlpha(0.18f)); g.fillRect(b.reduced(b.getWidth() * 0.12f)); }
                break;
            }

            case Kind::Ratio:
                if (on)
                {
                    g.setColour(onColour.withAlpha(0.20f));
                    g.fillRoundedRectangle(b.reduced(3 * s), 4 * s);
                    const auto c = Point<float>(b.getCentreX(), b.getY() + 7 * s);
                    g.setGradientFill(ColourGradient(onColour.withAlpha(0.6f), c.x, c.y, onColour.withAlpha(0.0f), c.x + 18 * s, c.y, true));
                    g.fillEllipse(c.x - 18 * s, c.y - 18 * s, 36 * s, 36 * s);
                    g.setColour(onColour.brighter(0.5f));
                    g.fillRoundedRectangle(c.x - 9 * s, c.y - 3 * s, 18 * s, 6 * s, 2 * s);
                }
                if (down) { g.setColour(Colours::black.withAlpha(0.25f)); g.fillRoundedRectangle(b.reduced(3 * s), 4 * s); }
                break;
        }
        ignoreUnused(over);
    }

private:
    Kind kind;
    String label;
    float dw, dh;
};

//==============================================================================
// Popup del tasto destro sulle manopole EQ: frequenza + shelf/bell (High, Low) oppure Q (Mid).
class EqPopup : public Component
{
public:
    EqPopup(AudioProcessorValueTreeState& apvts, const String& title, const String& freqId,
            const String& secondId, bool secondIsShelf)
    {
        titleLabel.setText(title, dontSendNotification);
        titleLabel.setFont(Font(FontOptions(16.0f, Font::bold)));
        addAndMakeVisible(titleLabel);

        freq.setSliderStyle(Slider::LinearHorizontal);
        freq.setTextBoxStyle(Slider::TextBoxRight, false, 90, 22);
        freq.setTextValueSuffix(" Hz");
        addAndMakeVisible(freq);
        freqAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, freqId, freq);

        if (secondIsShelf)
        {
            shelf.setButtonText("Shelf (off = Bell)");
            addAndMakeVisible(shelf);
            shelfAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(apvts, secondId, shelf);
        }
        else
        {
            q.setSliderStyle(Slider::LinearHorizontal);
            q.setTextBoxStyle(Slider::TextBoxRight, false, 90, 22);
            addAndMakeVisible(q);
            qAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, secondId, q);
            qLabel.setText("Q", dontSendNotification);
            addAndMakeVisible(qLabel);
        }
        setSize(340, 120);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(12);
        titleLabel.setBounds(r.removeFromTop(24));
        freq.setBounds(r.removeFromTop(28));
        r.removeFromTop(6);
        auto row = r.removeFromTop(28);
        shelf.setBounds(row);
        qLabel.setBounds(row.removeFromLeft(24));
        q.setBounds(row);
    }

private:
    Label titleLabel, qLabel;
    Slider freq, q;
    ToggleButton shelf;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> freqAtt, qAtt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> shelfAtt;
};

//==============================================================================
// Levetta POWER del Pultec: leva in giu' = acceso, in su = spento (il modulo EQ va in bypass).
class PowerSwitch : public Component, public SettableTooltipClient
{
public:
    explicit PowerSwitch(Image t) : tex(std::move(t)) { setMouseCursor(MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    bool on = true;

    void paint(Graphics& g) override
    {
        if (! tex.isValid()) return;
        g.setImageResamplingQuality(Graphics::highResamplingQuality);
        const auto b = getLocalBounds().toFloat();
        auto t = AffineTransform::scale(b.getWidth() / (float) tex.getWidth(), b.getHeight() / (float) tex.getHeight());
        if (! on)   // leva in su: la stessa immagine capovolta attorno al centro della ghiera
            t = t.followedBy(AffineTransform::rotation(MathConstants<float>::pi, b.getCentreX(), b.getHeight() * 50.0f / 120.0f));
        g.drawImageTransformed(tex, t);
    }
    void mouseUp(const MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    Image tex;
};

//==============================================================================
// Linguetta nell'angolo in basso a destra dell'EQ: l'angolo del pannello e' "sollevato" e lascia
// vedere l'altro equalizzatore sotto. Cliccandola si apre la scelta del tipo di EQ.
class EqCurl : public Component, public SettableTooltipClient
{
public:
    EqCurl() { setMouseCursor(MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity(true); }
    std::function<void()> onClick;
    bool pultec = false;   // EQ attuale (true = pannello blu)
    Colour frontCol { 0xffc8661a }, underCol { 0xff344856 };   // pannello attuale / quello che si vede sotto

    void paint(Graphics& g) override
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        const float s = (isMouseOver() ? 0.86f : 0.66f) * jmin(w, h);
        const Colour front = frontCol, under = underCol;

        // l'altro pannello che si vede sotto l'angolo sollevato
        Path reveal;
        reveal.addTriangle(w - s, h, w, h - s, w, h);
        g.setGradientFill(ColourGradient(under.brighter(0.15f), w - s * 0.5f, h - s * 0.5f, under.darker(0.5f), w, h, false));
        g.fillPath(reveal);
        // ombra del lembo sul pannello sotto
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.55f), w - s * 0.5f, h - s * 0.5f,
                                         Colours::transparentBlack, w - s * 0.2f, h - s * 0.2f, false));
        g.fillPath(reveal);

        // il lembo ripiegato (retro del pannello): un triangolo speculare rispetto alla piega
        Path flap;
        flap.addTriangle(w - s, h, w, h - s, w - s, h - s);
        Path shadow(flap);
        shadow.applyTransform(AffineTransform::translation(-2.5f, -2.5f));
        g.setColour(Colours::black.withAlpha(0.35f));
        g.fillPath(shadow);
        g.setGradientFill(ColourGradient(front.brighter(0.35f), w - s * 0.55f, h - s * 0.55f,
                                         front.darker(0.25f), w - s, h - s, false));
        g.fillPath(flap);
        g.setColour(Colours::white.withAlpha(0.35f));
        g.drawLine(w - s, h, w, h - s, 1.2f);          // filo di luce sulla piega
        g.setColour(Colours::black.withAlpha(0.45f));
        g.strokePath(flap, PathStrokeType(0.8f));

        // freccine sul lembo: "cambia"
        const float cx = w - s * 0.70f, cy = h - s * 0.70f, a = s * 0.10f;
        g.setColour(front.contrasting(0.75f).withAlpha(0.9f));
        Path arr;
        arr.addArrow({ cx - a, cy - a * 0.5f, cx + a, cy - a * 0.5f }, a * 0.25f, a * 0.8f, a * 0.6f);
        arr.addArrow({ cx + a, cy + a * 0.7f, cx - a, cy + a * 0.7f }, a * 0.25f, a * 0.8f, a * 0.6f);
        g.fillPath(arr);
    }

    bool hitTest(int x, int y) override { return (float) x + (float) y >= (float) getWidth() * 0.9f; }
    void mouseUp(const MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

//==============================================================================
// Finestrella di scelta dell'EQ (si apre dalla linguetta): due schede con l'anteprima dei pannelli.
// Freccina incisa sulla barra in basso: sposta il modulo a sinistra o a destra (scambio col vicino)
class ModuleArrow : public Component, public SettableTooltipClient
{
public:
    explicit ModuleArrow(int direction) : dir(direction) { setMouseCursor(MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity(true); }
    std::function<void()> onClick;
    bool enabledArrow = true;

    void paint(Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced(getWidth() * 0.18f);
        Path t;
        if (dir < 0) t.addTriangle(b.getX(), b.getCentreY(), b.getRight(), b.getY(), b.getRight(), b.getBottom());
        else         t.addTriangle(b.getRight(), b.getCentreY(), b.getX(), b.getY(), b.getX(), b.getBottom());
        const float a = enabledArrow ? (isMouseOver() ? 1.0f : 0.75f) : 0.18f;
        g.setColour(Colours::white.withAlpha(0.22f * a));            // incisione: filo di luce sotto
        g.fillPath(t, AffineTransform::translation(1.0f, 1.0f));
        g.setColour(Colour(0xff1c1915).withAlpha(a));
        g.fillPath(t);
    }
    void mouseUp(const MouseEvent& e) override { if (enabledArrow && e.mouseWasClicked() && onClick) onClick(); }

private:
    int dir;
};

class EqChooser : public Component
{
public:
    String title { "EQUALIZER TYPE" };
    bool showNames = true;   // scritte sotto le miniature (l'EQ non le usa: bastano le immagini dei pannelli)
    void setShowNames(bool show) { showNames = show; setSize(440, show ? 380 : 332); repaint(); }
    String names[2] { "WINTER EQ", "PULTEC" };
    String subs[2]  { "Winter Dreams curves", "Placebo-style" };

    EqChooser(Image meteoraThumb, Image pultecThumb, int current, Typeface::Ptr bold, Typeface::Ptr regular,
              std::function<void(int)> onPick)
        : thumbs { std::move(meteoraThumb), std::move(pultecThumb) }, selected(current), tfB(bold), tfR(regular), pick(std::move(onPick))
    {
        setSize(440, 380);
        setMouseCursor(MouseCursor::PointingHandCursor);
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff17191c));
        g.setColour(Colour(0xffe8dcc6));
        g.setFont(font(tfB, 17.0f, 0.12f));
        g.drawText(title, getLocalBounds().removeFromTop(44), Justification::centred);

        for (int i = 0; i < 2; ++i)
        {
            const auto r = card(i).toFloat();
            const bool sel = i == selected, over = i == hover;
            g.setColour(Colour(sel ? 0xff2a2620 : (over ? 0xff22252a : 0xff1d2024)));
            g.fillRoundedRectangle(r, 8.0f);
            g.setColour(sel ? Colour(0xffffb030) : Colour(over ? 0xff6b6258 : 0xff3a3d42));
            g.drawRoundedRectangle(r.reduced(1.0f), 8.0f, sel ? 2.5f : 1.2f);

            const auto& t = thumbs[(size_t) i];
            if (t.isValid())
            {
                const float th = 236.0f, tw = th * (float) t.getWidth() / (float) t.getHeight();
                g.setImageResamplingQuality(Graphics::highResamplingQuality);
                g.drawImage(t, { r.getCentreX() - tw * 0.5f, r.getY() + 12.0f, tw, th });
            }
            if (! showNames) continue;
            g.setColour(Colour(sel ? 0xffffd28a : 0xffe8dcc6));
            g.setFont(font(tfB, 15.0f, 0.08f));
            g.drawText(names[i], r.withTrimmedTop(254.0f).withHeight(22.0f), Justification::centred);
            g.setColour(Colour(0xffa89f92));
            g.setFont(font(tfR, 12.5f, 0.04f));
            g.drawText(subs[i], r.withTrimmedTop(276.0f).withHeight(18.0f), Justification::centred);
        }
    }

    void mouseMove(const MouseEvent& e) override { setHover(hit(e.getPosition())); }
    void mouseExit(const MouseEvent&) override   { setHover(-1); }
    void mouseUp(const MouseEvent& e) override
    {
        const int i = hit(e.getPosition());
        if (i < 0) return;
        selected = i;
        repaint();
        if (pick) pick(i);
        if (auto* box = findParentComponentOfClass<CallOutBox>()) box->dismiss();
    }

private:
    Rectangle<int> card(int i) const { return { 20 + i * 210, 50, 190, showNames ? 310 : 262 }; }
    int hit(Point<int> p) const { for (int i = 0; i < 2; ++i) if (card(i).contains(p)) return i; return -1; }
    void setHover(int i) { if (i != hover) { hover = i; repaint(); } }
    static Font font(const Typeface::Ptr& tf, float size, float kern)
    {
        Font f = tf != nullptr ? Font(FontOptions(tf)) : Font(FontOptions());
        f = f.withPointHeight(size);
        f.setExtraKerningFactor(kern);
        return f;
    }

    std::array<Image, 2> thumbs;
    int selected = 0, hover = -1;
    Typeface::Ptr tfB, tfR;
    std::function<void(int)> pick;
};

// Scale stampate sui quadranti: dB -> angolo della lancetta (gradi, 0 = verticale, positivo = destra).
// Misurate sulle etichette dei quadranti ridisegnati, cosi' la lancetta cade sul numero giusto.
constexpr float kDbPre[]   = { -20.f, -10.f, -7.f, -5.f, -3.f, -2.f, -1.f, 0.f, 1.f, 2.f, 3.f };            // preamp e FET
constexpr float kDegLls[]  = { -48.3f, -32.7f, -22.9f, -14.4f, -3.4f, 3.0f, 9.8f, 18.0f, 26.6f, 37.0f, 48.3f }; // LLS 4000
constexpr float kDegPre[]  = { -51.4f, -34.0f, -24.3f, -15.6f, -4.5f, 2.5f, 10.0f, 18.4f, 28.0f, 39.6f, 50.5f };
constexpr float kDbMaster[]  = { -20.f, -10.f, -7.f, -5.f, -3.f, -1.f, 0.f, 1.f, 2.f, 3.f };               // master e opto
constexpr float kDegMaster[] = { -44.5f, -33.2f, -21.4f, -12.5f, -3.0f, 5.7f, 14.6f, 22.9f, 33.2f, 44.1f };
constexpr float kDegOpto[]   = { -51.0f, -38.3f, -26.3f, -14.9f, -3.5f, 7.0f, 17.3f, 27.9f, 39.9f, 51.6f };

struct MeterSpec
{
    float px, py, len;                    // perno e lunghezza della lancetta (px immagine)
    Rectangle<float> clip;                // area visibile (esclude il "mozzo" in basso)
    LunchStripProcessor::Stage stage;
    bool isGainReduction;
    const float* dbTab;                   // scala stampata: dB ...
    const float* degTab;                  // ... e angolo corrispondente
    int n;
    float pos;                            // valore attuale in dB (con la balistica del VU)

    float angleDeg() const
    {
        if (pos <= dbTab[0]) return degTab[0];
        for (int i = 1; i < n; ++i)
            if (pos <= dbTab[i])
            {
                const float t = (pos - dbTab[i - 1]) / (dbTab[i] - dbTab[i - 1]);
                return degTab[i - 1] + t * (degTab[i] - degTab[i - 1]);
            }
        return degTab[n - 1];
    }
};
} // namespace

//==============================================================================
struct LunchStripEditor::Impl
{
    explicit Impl(LunchStripEditor& e) : ed(e), apvts(e.proc.apvts) {}

    LunchStripEditor& ed;
    AudioProcessorValueTreeState& apvts;
    Image background;
    Image bgScaled;        // sfondo gia' ridimensionato alla finestra (si rifa' solo quando cambia la misura)

    // ultimo stato disegnato: si ridisegna solo cio' che e' cambiato
    std::array<float, 4> drawnMeter { -1.0f, -1.0f, -1.0f, -1.0f };
    float drawnGlow = -1.0f, drawnRed = -1.0f;
    int   drawnLeds = -1;

    // --- valvola del preamp (finestrella sotto il VU) ---
    static constexpr int kTubeX = 172, kTubeY = 513, kTubeW = 130, kTubeH = 148;   // px immagine (finestrella + bordo)
    Image tubeHot, tubeCold;      // la valvola accesa (dalla foto) e la stessa spenta (vetro scuro)
    float heatA = 0.0f, heat = 0.0f;   // due stadi: il filamento si scalda con una curva a S
    float tubeLoad = 0.0f;             // un filo piu' luminosa quando il segnale spinge
    float flickerPhase = 0.0f, flicker = 1.0f;
    Random rng;

    // --- VU del preamp che arrossisce quando la saturazione e' troppa ---
    float vuRed = 0.0f;
    TooltipWindow tooltip { &ed, 700 };

    struct Placed { Component* c; float cx, cy, w, h; int mod = -2; };   // mod: -2 = dal punto, -1 = fisso, 0..3 = modulo 2..5

    //--- moduli 2..5 scambiabili (De-Esser, FET, EQ, Opto): rettangoli nello sfondo originale -------------
    struct Slot { float x, w; };
    static constexpr Slot kSlots[4] = { { 605.0f, 317.0f }, { 936.0f, 428.0f }, { 1372.0f, 402.0f }, { 1788.0f, 381.0f } };
    static constexpr float kSlotY = 158.0f, kSlotH = 1097.0f, kSlotsLeft = 605.0f, kSlotsRight = 2169.0f;
    int orderIndex() const { return jlimit(0, 23, (int) std::lround(apvts.getRawParameterValue("chain_order")->load())); }
    float dxOf(int m) const
    {
        if (m < 0 || m > 3) return 0.0f;
        const auto o = chain::order(orderIndex());
        float total = 0.0f;
        for (const auto& sl : kSlots) total += sl.w;
        const float gap = ((kSlotsRight - kSlotsLeft) - total) / 3.0f;
        float x = kSlotsLeft;
        for (int k : o)
        {
            if (k == m) return std::round(x - kSlots[m].x);
            x += kSlots[k].w + gap;
        }
        return 0.0f;
    }
    static int moduleAt(float cx, float cy)
    {
        if (cy < 150.0f || cy > 1262.0f) return -1;
        for (int m = 0; m < 4; ++m)
            if (cx >= kSlots[m].x - 4.0f && cx < kSlots[m].x + kSlots[m].w + 4.0f) return m;
        return -1;
    }
    int appliedOrder = -1;

    //--- modulo 1: Valve o LLS 4000 ----------------------------------------------------------------------
    std::vector<Component*> preValveComps, preLlsComps;
    std::unique_ptr<EqCurl> preCurl;
    std::unique_ptr<ParameterAttachment> preTypeAtt, orderAtt;
    std::vector<std::unique_ptr<ModuleArrow>> arrows;
    Image llsPanel, thumbValve, thumbLls, ledGreenOn, ledGreenOff, ledRedOn, ledRedOff;
    int appliedPreType = -1;
    int preType() const { return apvts.getRawParameterValue("pre_type")->load() > 0.5f ? 1 : 0; }
    bool drawnSignal = false, drawnPeak = false;
    std::vector<Placed> placed;
    std::vector<std::unique_ptr<KnobSlider>> knobs;
    std::vector<std::unique_ptr<HotButton>> buttons;
    std::vector<std::unique_ptr<SliderParameterAttachment>> sliderAtt;
    std::vector<std::unique_ptr<ButtonParameterAttachment>> buttonAtt;
    std::vector<std::unique_ptr<ParameterAttachment>> radioAtt;

    // lancette sui quadranti originali della foto
    std::array<MeterSpec, 4> meters {{
        { 338.f,  486.f, 135.f, { 156.f, 311.f, 356.f, 143.f },  LunchStripProcessor::Pre,    false, kDbPre,    kDegPre,    11, -20.0f },
        { 1150.f, 464.f, 119.f, { 1000.f, 300.f, 303.f, 120.f }, LunchStripProcessor::Fet,    true,  kDbPre,    kDegPre,    11,   0.0f },
        { 1978.f, 364.f, 125.f, { 1860.f, 226.f, 238.f, 129.f }, LunchStripProcessor::Opto,   true,  kDbMaster, kDegOpto,   10,   0.0f },   // gain reduction, come sull'LA-2A
        { 2370.f, 531.f, 122.f, { 2250.f, 366.f, 246.f, 140.f }, LunchStripProcessor::Master, false, kDbMaster, kDegMaster, 10, -20.0f },
    }};

    // VU dell'LLS 4000 (quadrante del pannello LLS)
    MeterSpec llsMeter { 340.0f, 496.5f, 125.0f, { 150.0f, 308.0f, 371.0f, 150.0f }, LunchStripProcessor::Pre, false, kDbPre, kDegLls, 11, -20.0f };
    const MeterSpec& preMeter() const { return preType() == 1 ? llsMeter : meters[0]; }
    static constexpr int kMeterMod[4] = { -1, 1, 3, -1 };   // FET e Opto si spostano con i loro moduli

    // --- font incorporati (DejaVu Sans Condensed): scritte vettoriali, nitide a ogni dimensione ---
    Typeface::Ptr tfBold, tfRegular;
    Font font(bool bold, float pointSize, float kerning = 0.0f) const
    {
        const auto& tf = bold ? tfBold : tfRegular;
        Font f = tf != nullptr ? Font(FontOptions(tf)) : Font(FontOptions());
        f = f.withPointHeight(pointSize);
        f.setExtraKerningFactor(kerning);
        return f;
    }

    // --- immagine della grafica statica (sfondo + scritte + scale + ombre), rifatta solo quando serve ---
    Image artCache, pultecPanel, thumbMeteora, thumbPultec, pultecLed;
    int artType = -1;

    // --- EQ: pannello arancione (Meteora) o Pultec ---
    std::vector<Component*> eqMeteoraComps, eqPultecComps;
    std::unique_ptr<EqCurl> curl;
    std::unique_ptr<PowerSwitch> power;
    std::unique_ptr<ParameterAttachment> eqTypeAtt, powerAtt;
    int eqType() const { return apvts.getRawParameterValue("eq_type")->load() > 0.5f ? 1 : 0; }

    struct ShadowSpec { float cx, cy, r; int eqType; };
    std::vector<ShadowSpec> shadows;

    //-------------------------------------------------------------------------
    HotButton* addButton(HotButton::Kind k, const String& text, float cx, float cy, float w, float h,
                         const String& tip, const String& paramId = {})
    {
        auto b = std::make_unique<HotButton>(k, text, w, h);
        b->setTooltip(tip);
        auto* raw = b.get();
        ed.addAndMakeVisible(raw);
        placed.push_back({ raw, cx, cy, w, h });
        if (paramId.isNotEmpty())
            buttonAtt.push_back(std::make_unique<ButtonParameterAttachment>(*apvts.getParameter(paramId), *raw));
        buttons.push_back(std::move(b));
        return raw;
    }

    // Pulsante che imposta un valore fisso su un parametro (usato come "radio"): CLEAN, SOFT, RATIO.
    HotButton* addRadio(HotButton::Kind k, const String& text, float cx, float cy, float w, float h,
                        const String& tip, const String& paramId, float valueWhenOn, bool isChoiceCompare)
    {
        auto* btn = addButton(k, text, cx, cy, w, h, tip);
        btn->setClickingTogglesState(false);
        auto* param = apvts.getParameter(paramId);
        auto att = std::make_unique<ParameterAttachment>(*param, [btn, valueWhenOn, isChoiceCompare](float v)
        {
            btn->setToggleState(isChoiceCompare ? (int) std::round(v) == (int) valueWhenOn
                                                : (v > 0.5f) == (valueWhenOn > 0.5f), dontSendNotification);
        });
        auto* attRaw = att.get();
        btn->onClick = [attRaw, valueWhenOn] { attRaw->setValueAsCompleteGesture(valueWhenOn); };
        att->sendInitialUpdate();
        radioAtt.push_back(std::move(att));
        return btn;
    }

    std::array<HotButton*, 4> ratioButtons {};

    void refreshRatioButtons()
    {
        const bool all = apvts.getRawParameterValue("fet_parallel")->load() > 0.5f;
        const int sel = (int) std::lround(apvts.getRawParameterValue("fet_ratio")->load());
        for (int i = 0; i < 4; ++i)
            if (auto* b = ratioButtons[(size_t) i])
                b->setToggleState(all || i == sel, dontSendNotification);
    }

    HotButton* addPresetButton(HotButton::Kind k, float cx, float cy, float w, float h, const String& tip,
                               const String& paramId, LunchStripProcessor::FetPreset which)
    {
        auto* btn = addButton(k, "", cx, cy, w, h, tip);
        btn->setClickingTogglesState(false);
        auto att = std::make_unique<ParameterAttachment>(*apvts.getParameter(paramId),
                                                         [btn](float v) { btn->setToggleState(v > 0.5f, dontSendNotification); });
        btn->onClick = [this, btn, which] { ed.proc.setFetPreset(which, ! btn->getToggleState()); };
        att->sendInitialUpdate();
        radioAtt.push_back(std::move(att));
        return btn;
    }

    KnobSlider* addKnob(const gui::KnobSpec& k, int forEqType = -1)
    {
        const String id(k.paramId);
        Image tex = loadImage(String("knob_") + k.tex + (k.hasCap ? "_ring_png" : "_png"));
        Image cap = k.hasCap ? loadImage(String("knob_") + k.tex + "_cap_png") : Image();
        auto knob = std::make_unique<KnobSlider>(tex, cap, k.baseDeg);
        knob->setRotaryRange(k.startDeg, k.endDeg);
        knob->setTooltip(k.tip);
        auto* raw = knob.get();
        ed.addAndMakeVisible(raw);
        raw->setPopupDisplayEnabled(true, false, &ed);
        sliderAtt.push_back(std::make_unique<SliderParameterAttachment>(*apvts.getParameter(id), *raw));
        if (auto* p = apvts.getParameter(id))
            raw->setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));

        if (k.eqPopup != 0)
        {
            raw->onRightClick = [this, raw, id]
            {
                if (id == "eq_high_gain")   // HIGH: menu semplice Bell / High shelf (la frequenza e' sul selettore)
                {
                    auto* sp = apvts.getParameter("eq_high_shelf");
                    const bool isShelf = sp->getValue() > 0.5f;
                    PopupMenu menu;
                    menu.addItem(1, "High shelf", true, isShelf);
                    menu.addItem(2, "Bell", true, ! isShelf);
                    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(raw), [sp](int choice)
                    {
                        if (choice == 0) return;
                        sp->beginChangeGesture();
                        sp->setValueNotifyingHost(choice == 1 ? 1.0f : 0.0f);
                        sp->endChangeGesture();
                    });
                    return;
                }
                String title, freqId, secondId; bool shelf = true;
                if      (id == "eq_high_gain") { title = "HIGH";  freqId = "eq_high_freq"; secondId = "eq_high_shelf"; }
                else if (id == "eq_low_gain")  { title = "LOW";   freqId = "eq_low_freq";  secondId = "eq_low_shelf"; }
                else                           { title = "MID";   freqId = "eq_mid_freq";  secondId = "eq_mid_q"; shelf = false; }   // (non raggiungibile: il Mid non ha popup)
                CallOutBox::launchAsynchronously(std::make_unique<EqPopup>(apvts, title, freqId, secondId, shelf),
                                                 raw->getScreenBounds(), nullptr);
            };
        }
        placed.push_back({ raw, k.cx, k.cy, k.size, k.size });
        knobs.push_back(std::move(knob));
        return raw;
    }

    // manopola a scatti (selettore di frequenza): ogni posizione corrisponde a un valore del parametro
    KnobSlider* addSteppedKnob(const String& texName, const char* paramId, float cx, float cy, float size, float baseDeg,
                               std::vector<float> values, std::vector<float> angles, std::vector<String> labels,
                               const String& tip)
    {
        const int n = (int) values.size();
        auto knob = std::make_unique<KnobSlider>(loadImage(texName), Image(), baseDeg);
        knob->setRange(0.0, (double) (n - 1), 1.0);
        knob->setStepAngles(angles);
        knob->setMouseDragSensitivity(60 + 24 * n);
        knob->setTooltip(tip);
        auto* raw = knob.get();
        ed.addAndMakeVisible(raw);
        raw->setPopupDisplayEnabled(true, false, &ed);
        raw->textFromValueFunction = [labels](double v) { return labels[(size_t) jlimit(0, (int) labels.size() - 1, roundToInt((float) v))]; };

        auto* param = apvts.getParameter(paramId);
        auto nearest = [values](float v)
        {
            int best = 0; float bd = 1.0e30f;
            for (int i = 0; i < (int) values.size(); ++i)
                if (std::abs(values[(size_t) i] - v) < bd) { bd = std::abs(values[(size_t) i] - v); best = i; }
            return best;
        };
        auto att = std::make_unique<ParameterAttachment>(*param, [raw, nearest](float v) { raw->setValue((double) nearest(v), dontSendNotification); });
        auto* attRaw = att.get();
        raw->onValueChange = [attRaw, raw, values] { attRaw->setValueAsCompleteGesture(values[(size_t) jlimit(0, (int) values.size() - 1, roundToInt((float) raw->getValue()))]); };
        raw->setDoubleClickReturnValue(true, (double) nearest(param->convertFrom0to1(param->getDefaultValue())));
        att->sendInitialUpdate();
        radioAtt.push_back(std::move(att));

        placed.push_back({ raw, cx, cy, size, size });
        knobs.push_back(std::move(knob));
        return raw;
    }

    // manopola del Pultec: texture qualsiasi, corsa propria
    KnobSlider* addPultecKnob(const char* texName, const char* paramId, float px, float py, float sizePanel,
                              float baseDeg, float startDeg, float endDeg, const char* tip)
    {
        constexpr float k = 400.0f / 624.0f, ox = 1374.0f, oy = 157.0f;   // pannello 624 x 1712 -> strip
        auto knob = std::make_unique<KnobSlider>(loadImage(texName), Image(), baseDeg);
        knob->setRotaryRange(startDeg, endDeg);
        knob->setTooltip(tip);
        auto* raw = knob.get();
        ed.addAndMakeVisible(raw);
        raw->setPopupDisplayEnabled(true, false, &ed);
        sliderAtt.push_back(std::make_unique<SliderParameterAttachment>(*apvts.getParameter(paramId), *raw));
        if (auto* p = apvts.getParameter(paramId))
            raw->setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        const float cx = ox + px * k, cy = oy + py * k, sz = sizePanel * k;
        placed.push_back({ raw, cx, cy, sz, sz });
        eqPultecComps.push_back(raw);
        knobs.push_back(std::move(knob));
        return raw;
    }

    void build()
    {
        background = loadImage("background_jpg");
        buildTubeImages();
        auto loadFont = [](const char* name) -> Typeface::Ptr
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource(name, size))
                return Typeface::createSystemTypefaceFor(data, (size_t) size);
            return {};
        };
        tfBold    = loadFont("font_bold_ttf");
        tfRegular = loadFont("font_regular_ttf");
        pultecPanel  = loadImage("pultec_panel_jpg");
        thumbMeteora = loadImage("eq_thumb_meteora_jpg");
        thumbPultec  = loadImage("eq_thumb_pultec_jpg");

        for (const auto& k : gui::kKnobs)
        {
            const bool isEq = String(k.paramId).startsWith("eq_");
            auto* kn = addKnob(k, isEq ? 0 : -1);
            if (isEq) eqMeteoraComps.push_back(kn);
            if (String(k.paramId).startsWith("pre_")) preValveComps.push_back(kn);
        }

        // EQ arancione (Neve): selettori di frequenza a scatti, con i valori stampati sul pannello
        {
            auto addSel = [this](const char* tex, const char* param, float cx, float cy, std::vector<float> v, std::vector<float> a,
                                 std::vector<String> l, const char* tip)
            {
                eqMeteoraComps.push_back(addSteppedKnob(String("knob_") + tex + "_png", param, cx, cy, 108.0f, 0.0f, std::move(v), std::move(a), std::move(l), tip));
            };
            addSel("eq_high_freq", "eq_high_freq", 1481.f, 362.f, { 8000.f, 10000.f, 12000.f, 16000.f }, { -60.f, -30.f, 30.f, 60.f },
                   { "8 kHz", "10 kHz", "12 kHz", "16 kHz" }, "High: frequency");
            addSel("eq_mid_freq", "eq_mid_freq", 1482.f, 599.f, { 250.f, 360.f, 500.f, 700.f, 1000.f, 1600.f, 3200.f, 4800.f, 7200.f },
                   { -120.f, -90.f, -60.f, -30.f, 0.f, 30.f, 60.f, 90.f, 120.f },
                   { "250 Hz", "360 Hz", "500 Hz", "700 Hz", "1 kHz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" }, "Mid: frequency");
            addSel("eq_low_freq", "eq_low_freq", 1482.f, 841.f, { 35.f, 60.f, 110.f, 220.f }, { -55.f, -19.f, 19.f, 55.f },
                   { "35 Hz", "60 Hz", "110 Hz", "220 Hz" }, "Low: frequency");
            addSel("eq_hpf", "eq_hpf", 1481.f, 1089.f, { 20.f, 50.f, 80.f, 160.f, 300.f }, { -60.f, -30.f, 0.f, 30.f, 60.f },
                   { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, "HPF");
        }

        using K = HotButton::Kind;

        // --- Trasformatore d'ingresso (scelta unica, sulla barra) ---
        auto* clean = addRadio(K::Pill, "CLEAN", 680, 106, 104, 36, "Clean transformer (Jensen-style)", "xfmr_neve", 0.0f, false);
        auto* neve  = addRadio(K::Pill, "NEVE",  795, 106, 104, 36, "Neve 1073 transformer", "xfmr_neve", 1.0f, false);
        clean->engraved = neve->engraved = true;

        // --- Preamp ---
        // il LED +48 dell'immagine e' stato tolto: al suo posto il bypass del preamp
        {
            auto* b1 = addButton(K::Pill, "BYP", 406, 1145, 64, 30, "Preamp bypass (lit = bypassed)", "pre_bypass");
            b1->onColour = Colour(0xffff5040);
            preValveComps.push_back(b1);
            preValveComps.push_back(addButton(K::Dot, "", 283, 1142, 44, 62, "Phase (invert polarity)", "pre_phase"));
            preValveComps.push_back(addButton(K::Dot, "", 512, 1143, 44, 62, "Pad -20 dB", "pre_pad"));
        }

        // --- De-esser ---
        addButton(K::Ring, "", 700, 1128, 70, 70, "Listen: hear the detected band", "de_listen");
        addButton(K::DimWhenOff, "", 827, 1128, 60, 60, "De-esser bypass (lit = bypassed)", "de_bypass")->offColour = Colour(0xff2a2010);

        // --- FET compressor ---
        // RATIO: con il preset PARALLEL acceso si accendono tutti e quattro ("all buttons" del 1176)
        static const char* ratios[4] = { "4", "8", "12", "20" };
        for (int i = 0; i < 4; ++i)
        {
            auto* btn = addButton(K::Ratio, "", 1020.f + 84.3f * (float) i, 997, 58, 52, String("Ratio ") + ratios[i] + ":1");
            btn->setClickingTogglesState(false);
            ratioButtons[(size_t) i] = btn;
            btn->onClick = [this, i]
            {
                ed.proc.leaveFetParallel();
                if (auto* p = apvts.getParameter("fet_ratio"))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1((float) i));
                    p->endChangeGesture();
                }
            };
        }
        for (const char* id : { "fet_ratio", "fet_parallel" })
        {
            auto att = std::make_unique<ParameterAttachment>(*apvts.getParameter(id), [this](float) { refreshRatioButtons(); });
            att->sendInitialUpdate();
            radioAtt.push_back(std::move(att));
        }

        // VOCAL (LED) e PARALLEL (levetta): preset. Acceso = carica il preset, spento = torna alle tue manopole.
        addPresetButton(K::DimWhenOff, 1147, 1155, 46, 46, "VOCAL preset (turn off to return to your own settings)",
                        "fet_vocal", LunchStripProcessor::FetPreset::Vocal)->offColour = Colour(0xff2a2010);
        addPresetButton(K::Dot, 1274, 1155, 44, 62, "PARALLEL preset: heavy compression, all buttons, Dry/Wet 50%",
                        "fet_parallel", LunchStripProcessor::FetPreset::Parallel);

        // --- Opto ---
        addButton(K::Dot, "", 1979, 1112, 60, 62, "Compress / Limit (lit = Limit)", "opto_limit")->dotOffset = { 0.0f, 30.0f };

        // --- Master ---
        addButton(K::DimWhenOff, "", 2371, 1021, 56, 56, "Limiter", "master_limiter")->offColour = Colour(0xff2a2010);
        addButton(K::Ring, "", 2289, 1149, 66, 66, "Stereo link (lit = limiter acts on both channels together)", "master_link");
        addButton(K::Ring, "", 2450, 1149, 66, 66, "Master bypass", "master_bypass")->onColour = Colour(0xffff5040);

        // --- Pulsanti AG / BYP dove l'immagine non li ha ---
        struct Panel { const char* prefix; float right; bool bypass; };
        for (const auto& p : { Panel{ "pre", 595, false }, Panel{ "de", 920, false }, Panel{ "fet", 1362, true },
                               Panel{ "eq", 1776, true }, Panel{ "opto", 2172, true } })
        {
            float x = p.right - 98.0f;
            if (p.bypass)
            {
                auto* b = addButton(K::Pill, "BYP", x, 180, 56, 27, "Bypass (lit = bypassed)", String(p.prefix) + "_bypass");
                b->onColour = Colour(0xffff5040);
                // BYP e AG dell'EQ restano visibili con entrambi i tipi di EQ (Winter e Pultec)
                x -= 62.0f;
            }
            auto* ag = addButton(K::Pill, "AG", x, 180, 56, 27, "Module auto gain", String(p.prefix) + "_autogain");
            (void) ag;
        }

        buildPultec();   // per ultimo: nasconde/mostra i controlli dell'EQ giusto
        buildLls();      // modulo 1: Valve o LLS 4000
        buildArrows();   // ordine dei moduli 2..5
    }

    void buildPultec()
    {
        // pannello 624 x 1712: coordinate misurate sull'immagine (vedi Assets/pultec_panel.jpg)
        {
            constexpr float kp = 400.0f / 624.0f, ox = 1374.0f, oy = 157.0f;
            eqPultecComps.push_back(addSteppedKnob("pultec_knob_lowfreq_png", "pul_lf_freq", ox + 163.0f * kp, oy + 293.0f * kp, 152.0f * kp, -136.0f,
                                                   { 0.f, 1.f, 2.f, 3.f }, { -60.f, -20.f, 20.f, 60.f }, { "20 Hz", "30 Hz", "60 Hz", "100 Hz" },
                                                   "LOW FREQ: 20 / 30 / 60 / 100 Hz"));
            // baseDeg -136: l'indicatore bianco della levetta sta all'estremita' in basso a sinistra, e' lui che deve puntare la scritta
            // il DSP ha anche 4 kHz (indice 1): sul pannello ci sono 3 / 5 / 8 / 10 / 12 / 16
            eqPultecComps.push_back(addSteppedKnob("pultec_knob_highfreq_png", "pul_hf_freq", ox + 465.0f * kp, oy + 293.0f * kp, 152.0f * kp, -136.0f,
                                                   { 0.f, 2.f, 3.f, 4.f, 5.f, 6.f }, { -90.f, -54.f, -18.f, 18.f, 54.f, 90.f },
                                                   { "3 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" },
                                                   "HIGH FREQ: 3 / 5 / 8 / 10 / 12 / 16 kHz"));
        }
        addPultecKnob("pultec_knob_big_png", "pul_lf_boost", 164,  598, 196, 0.0f, -148.0f, 148.0f, "LF BOOST");
        addPultecKnob("pultec_knob_big_png", "pul_lf_atten", 466,  602, 196, 0.0f, -148.0f, 148.0f, "LF ATTEN");
        addPultecKnob("pultec_knob_big_png", "pul_hf_bw",    314,  917, 196, 0.0f, -148.0f, 148.0f, "BANDWIDTH of the HF boost: SHARP .. BROAD");
        addPultecKnob("pultec_knob_big_png", "pul_hf_boost", 162, 1259, 196, 0.0f, -148.0f, 148.0f, "HF BOOST");
        addPultecKnob("pultec_knob_big_png", "pul_hf_atten", 466, 1258, 196, 0.0f, -148.0f, 148.0f, "HF ATTEN (10 kHz)");
        addPultecKnob("pultec_knob_big_png", "pul_drive",    140, 1592, 131, 0.0f, -148.0f, 148.0f, "DRIVE with auto gain");

        // POWER: levetta = bypass dell'EQ (giu' = acceso)
        constexpr float k = 400.0f / 624.0f;
        power = std::make_unique<PowerSwitch>(loadImage("pultec_switch_png"));
        power->setTooltip("POWER: EQ on / off");
        ed.addAndMakeVisible(power.get());
        placed.push_back({ power.get(), 1374.0f + 483.0f * k, 157.0f + 1563.0f * k, 100.0f * k, 120.0f * k });
        eqPultecComps.push_back(power.get());
        powerAtt = std::make_unique<ParameterAttachment>(*apvts.getParameter("eq_bypass"), [this](float v)
        {
            power->on = v < 0.5f;
            power->repaint();
            ed.repaint(area({ 1374.0f + dxOf(2) + 312.0f * (400.0f / 624.0f) - 45.0f, 157.0f + 1555.0f * (400.0f / 624.0f) - 45.0f, 90.0f, 90.0f }));   // LED
        });
        power->onClick = [this] { powerAtt->setValueAsCompleteGesture(power->on ? 1.0f : 0.0f); };
        powerAtt->sendInitialUpdate();

        // linguetta nell'angolo in basso a destra: apre la scelta dell'EQ
        curl = std::make_unique<EqCurl>();
        curl->setTooltip("Switch EQ: Winter (orange) or Pultec");
        ed.addAndMakeVisible(curl.get());
        placed.push_back({ curl.get(), 1739.0f, 1220.0f, 70.0f, 70.0f });
        curl->onClick = [this]
        {
            auto chooser = std::make_unique<EqChooser>(thumbMeteora, thumbPultec, eqType(), tfBold, tfRegular, [this](int t)
            {
                if (auto* p = apvts.getParameter("eq_type"))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1((float) t));
                    p->endChangeGesture();
                }
            });
            chooser->setShowNames(false);   // niente scritte "WINTER EQ" / "PULTEC": solo i pannelli
            CallOutBox::launchAsynchronously(std::move(chooser), curl->getScreenBounds(), nullptr);
        };

        eqTypeAtt = std::make_unique<ParameterAttachment>(*apvts.getParameter("eq_type"), [this](float) { applyEqType(); });
        eqTypeAtt->sendInitialUpdate();
    }

    int appliedEqType = -1;

    void applyEqType()
    {
        const int t = eqType();
        appliedEqType = t;
        for (auto* c : eqMeteoraComps) c->setVisible(t == 0);
        for (auto* c : eqPultecComps)  c->setVisible(t == 1);
        if (curl != nullptr)
        {
            curl->pultec = t == 1;
            curl->frontCol = t == 1 ? Colour(0xff344856) : Colour(0xffc8661a);
            curl->underCol = t == 1 ? Colour(0xffc8661a) : Colour(0xff344856);
            curl->repaint();
        }
        artType = -1;   // la grafica statica va rifatta
        ed.repaint();
    }

    Rectangle<int> area(Rectangle<float> r) const
    {
        const float sc = (float) ed.getWidth() / gui::kImgW;
        return r.transformedBy(AffineTransform::scale(sc)).getSmallestIntegerContainer().expanded(2);
    }

    //-------------------------------------------------------------------------
    // Grafica statica in coordinate dell'immagine (2624 x 1404): ombre delle manopole, scale, scritte, scale dei VU.
    void drawText(Graphics& g, const String& text, float x, float y, float size, bool bold, uint32 argb, bool lightText,
                  float tracking, char align) const
    {
        g.setFont(font(bold, size, tracking));
        const float hgt = size * 2.0f, wid = 900.0f;
        Rectangle<float> r;
        Justification j = Justification::centred;
        if (align == 'l')      { r = { x, y - hgt * 0.5f, wid, hgt };              j = Justification::centredLeft; }
        else if (align == 'r') { r = { x - wid, y - hgt * 0.5f, wid, hgt };        j = Justification::centredRight; }
        else                   { r = { x - wid * 0.5f, y - hgt * 0.5f, wid, hgt }; }
        g.setColour(lightText ? Colours::black.withAlpha(0.55f) : Colours::white.withAlpha(0.18f));
        g.drawText(text, r.translated(1.2f, 1.2f), j, false);
        g.setColour(Colour(argb));
        g.drawText(text, r, j, false);
    }

    // Grafica disegnata sopra la foto (in coordinate dell'immagine): solo cio' che la foto non ha.
    void renderArt(Graphics& g, int type) const
    {
        // ombre sotto le manopole del Pultec (il pannello blu non ha manopole stampate)
        for (const auto& sh : shadows)
        {
            if (sh.eqType != 1 || type != 1) continue;
            const float r = sh.r, cx = sh.cx + dxOf(2) + 0.07f * r, cy = sh.cy + 0.13f * r, R = r * 1.28f;
            ColourGradient grad(Colours::black.withAlpha(0.55f), cx, cy, Colours::transparentBlack, cx + R, cy, true);
            grad.addColour(0.72, Colours::black.withAlpha(0.42f));
            g.setGradientFill(grad);
            g.fillEllipse(cx - R, cy - R, 2 * R, 2 * R);
        }

        // titolo del selettore trasformatore (inciso sulla barra)
        drawText(g, "INPUT TRANSFORMER", 440.0f, 106.0f, 21.0f, true, 0xff2a251f, false, 0.06f, 'c');
    }

    void buildTubeImages()
    {
        if (! background.isValid()) return;
        const Rectangle<int> tubeArea(kTubeX, kTubeY, kTubeW, kTubeH);
        tubeHot  = background.getClippedImage(tubeArea).createCopy();
        tubeCold = Image(Image::RGB, kTubeW, kTubeH, false);
        Image::BitmapData src(tubeHot, Image::BitmapData::readOnly);
        Image::BitmapData dst(tubeCold, Image::BitmapData::writeOnly);
        for (int y = 0; y < kTubeH; ++y)
            for (int x = 0; x < kTubeW; ++x)
            {
                const auto c = src.getPixelColour(x, y);
                const float r = c.getFloatRed(), gr = c.getFloatGreen(), b = c.getFloatBlue();
                // vetro spento: dove la foto e' arancio (luce della valvola) resta solo un grigio molto scuro
                // delle parti metalliche; il pannello blu attorno non cambia
                const float lum = 0.30f * r + 0.59f * gr + 0.11f * b;
                const float v = 0.035f + 0.22f * lum;
                const float o = jlimit(0.0f, 1.0f, (r - b - 0.01f) * 8.0f);
                dst.setPixelColour(x, y, Colour::fromFloatRGBA(r + o * (v * 0.95f - r), gr + o * (v * 0.93f - gr),
                                                               b + o * (v - b), 1.0f));
            }
    }

    void updateTube(float dt)
    {
        const bool preOn = apvts.getRawParameterValue("pre_bypass")->load() < 0.5f && preType() == 0;
        const float target = (preOn && ed.proc.isProcessingAudio()) ? 1.0f : 0.0f;
        // si accende in ~2 s (curva a S come un filamento vero), si spegne un po' piu' lentamente
        const float tau = target > heatA ? 0.7f : 1.1f;
        const float k = 1.0f - std::exp(-dt / tau);
        heatA += k * (target - heatA);
        heat  += k * (heatA - heat);

        // carico: come la valvola di Marley, piu' luminosa quando satura (da -28 a -4 dB di distorsione)
        const float distDb = ed.proc.getModule(LunchStripProcessor::Pre).getDistortionDb();
        const float loadTarget = preOn ? jlimit(0.0f, 1.0f, (distDb + 28.0f) / 24.0f) : 0.0f;
        tubeLoad += (loadTarget - tubeLoad) * (loadTarget > tubeLoad ? 0.22f : 0.07f);

        // tremolio lento del filamento (alimentazione) + piccolo rumore
        flickerPhase += dt * 6.3f;
        if (flickerPhase > MathConstants<float>::twoPi * 100.0f) flickerPhase = 0.0f;
        flicker = 1.0f + 0.025f * std::sin(flickerPhase * 2.1f) + 0.015f * std::sin(flickerPhase * 5.3f)
                       + 0.02f * (rng.nextFloat() - 0.5f);
    }

    void updateVuRed()
    {
        // come Marley: il vetro inizia ad arrossarsi a -6 dB di distorsione ed e' rosso pieno a -1 dB
        // (MarleyEngine::distortionDb). Attacco rapido, rilascio lento.
        constexpr float distStartDb = -6.0f, distFullDb = -1.0f;
        const float distDb = ed.proc.getModule(LunchStripProcessor::Pre).getDistortionDb();
        const float over = jlimit(0.0f, 1.0f, (distDb - distStartDb) / (distFullDb - distStartDb));
        vuRed += (over - vuRed) * (over > vuRed ? 0.35f : 0.06f);
        if (vuRed < 0.002f) vuRed = 0.0f;
    }

    void paintTube(Graphics& g)
    {
        if (! tubeHot.isValid()) return;
        const Rectangle<float> win(180.0f, 520.0f, 114.0f, 135.0f);   // vetro della finestrella
        const auto c = Point<float>(235.0f, 592.0f);   // centro del filamento
        const float glow = jlimit(0.0f, 1.3f, heat * flicker * (0.85f + 0.3f * tubeLoad));

        // vetro spento -> valvola accesa (la foto) con l'intensita' del filamento
        g.drawImageAt(tubeCold, kTubeX, kTubeY);
        if (heat > 0.002f)
        {
            g.setOpacity(jlimit(0.0f, 1.0f, heat * (0.93f + 0.07f * flicker)));
            g.drawImageAt(tubeHot, kTubeX, kTubeY);
            g.setOpacity(1.0f);
        }

        // luce che esce dalla finestrella sul pannello
        if (glow > 0.01f)
        {
            ColourGradient halo(Colour(0xffff8a2a).withAlpha(0.20f * glow), c.x, c.y,
                                Colour(0xffff8a2a).withAlpha(0.0f), c.x + 105.0f, c.y, true);
            g.setGradientFill(halo);
            g.fillEllipse(c.x - 105.0f, c.y - 100.0f, 210.0f, 200.0f);
        }

        Graphics::ScopedSaveState ss(g);
        Path clip; clip.addRoundedRectangle(win, 10.0f);
        g.reduceClipRegion(clip);

        if (glow > 0.01f)
        {
            // bagliore caldo dentro il vetro
            ColourGradient inner(Colour(0xffff9a3c).withAlpha(0.38f * glow), c.x, c.y,
                                 Colour(0xffff6a10).withAlpha(0.0f), c.x + 62.0f, c.y, true);
            g.setGradientFill(inner);
            g.fillRect(win);
            // cuore del filamento: piu' chiaro e giallo
            ColourGradient core(Colour(0xffffd27a).withAlpha(0.32f * glow * glow), c.x, c.y + 10.0f,
                                Colour(0xffffb050).withAlpha(0.0f), c.x + 22.0f, c.y + 10.0f, true);
            g.setGradientFill(core);
            g.fillEllipse(c.x - 22.0f, c.y - 45.0f, 44.0f, 110.0f);
        }

        // riflesso del vetro (c'e' anche a valvola spenta)
        ColourGradient refl(Colours::white.withAlpha(0.0f), win.getX() + 30.0f, 0.0f,
                            Colours::white.withAlpha(0.0f), win.getX() + 52.0f, 0.0f, false);
        refl.addColour(0.5, Colours::white.withAlpha(0.07f));
        g.setGradientFill(refl);
        g.fillRect(win.withTrimmedTop(20.0f).withTrimmedBottom(20.0f));
    }

    //--- quadranti dei VU: rosso della lampada e vetro --------------------------------------------------------
    struct Face { Rectangle<float> r; float radius; int mod; };   // mod: modulo 2..5 (0..3) che lo sposta, -1 fisso
    static constexpr float kValveFace[4] = { 149.0f, 306.0f, 372.0f, 182.0f };
    static constexpr float kLlsFace[4]   = { 147.0f, 305.0f, 378.0f, 181.0f };
    Rectangle<float> preFace() const
    {
        const auto* f = preType() == 1 ? kLlsFace : kValveFace;
        return { f[0], f[1], f[2], f[3] };
    }
    std::vector<Face> glassFaces() const
    {
        return { { preFace(), preType() == 1 ? 16.0f : 20.0f, -1 },
                 { { 990.0f, 292.0f, 320.0f, 156.0f }, 14.0f, 1 },     // FET
                 { { 1852.0f, 218.0f, 250.0f, 154.0f }, 8.0f, 3 },     // Opto
                 { { 2250.0f, 368.0f, 245.0f, 212.0f }, 10.0f, -1 } }; // Master
    }

    // il quadrante "acceso di rosso": stessa immagine del quadrante, ma la luce della lampada e' rossa
    // (i segni scuri restano scuri, il fondo diventa rosso; piu' intenso al centro come una lampadina)
    Image redValve, redLls;
    static Image makeRedFace(const Image& src, Rectangle<int> area)
    {
        if (! src.isValid()) return {};
        Image in = src.getClippedImage(area.getIntersection(src.getBounds())).createCopy();
        Image out(Image::RGB, in.getWidth(), in.getHeight(), false);
        Image::BitmapData s(in, Image::BitmapData::readOnly);
        Image::BitmapData d(out, Image::BitmapData::writeOnly);
        const float w = (float) in.getWidth(), h = (float) in.getHeight();
        for (int y = 0; y < in.getHeight(); ++y)
            for (int x = 0; x < in.getWidth(); ++x)
            {
                const auto c = s.getPixelColour(x, y);
                const float lum = (c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue()) / 3.0f;
                const float dist = std::hypot(((float) x - w * 0.5f) / (w * 0.5f), ((float) y - h * 0.62f) / (h * 0.75f));
                const float glow = jlimit(0.55f, 1.25f, 1.25f - 0.55f * dist) * 1.35f;
                d.setPixelColour(x, y, Colour::fromFloatRGBA(jmin(1.0f, lum * 1.0f * glow), jmin(1.0f, lum * 0.16f * glow),
                                                              jmin(1.0f, lum * 0.08f * glow), 1.0f));
            }
        return out;
    }
    void buildRedFaces()
    {
        redValve = makeRedFace(background, Rectangle<float>(kValveFace[0], kValveFace[1], kValveFace[2], kValveFace[3]).toNearestInt());
        // pannello LLS: 1108 x 2194 per 555 x 1097 nella strip
        const float kx = 1108.0f / 555.0f, ky = 2.0f;
        redLls = makeRedFace(llsPanel, Rectangle<float>((kLlsFace[0] - 50.0f) * kx, (kLlsFace[1] - 158.0f) * ky,
                                                        kLlsFace[2] * kx, kLlsFace[3] * ky).toNearestInt());
    }

    void paintVuRed(Graphics& g)
    {
        if (vuRed < 0.01f) return;
        const auto face = preFace();
        const auto& img = preType() == 1 ? redLls : redValve;
        Graphics::ScopedSaveState ss(g);
        Path p; p.addRoundedRectangle(face, preType() == 1 ? 16.0f : 20.0f);
        g.reduceClipRegion(p);
        if (img.isValid())
        {
            g.setImageResamplingQuality(Graphics::highResamplingQuality);
            g.setOpacity(jlimit(0.0f, 1.0f, vuRed));
            g.drawImage(img, face);
            g.setOpacity(1.0f);
        }
        else
        {
            g.setColour(Colour(0xffd40c04).withAlpha(0.7f * vuRed));
            g.fillPath(p);
        }
    }

    // vetro sopra il quadrante (disegnato dopo la lancetta, che cosi' sta "dietro il vetro")
    static void paintGlass(Graphics& g, Rectangle<float> f, float radius)
    {
        Graphics::ScopedSaveState ss(g);
        Path clip; clip.addRoundedRectangle(f, radius);
        g.reduceClipRegion(clip);
        const float x = f.getX(), y = f.getY(), w = f.getWidth(), h = f.getHeight();
        // ombra della cornice in alto
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.38f), x, y, Colours::transparentBlack, x, y + h * 0.20f, false));
        g.fillRect(f);
        // bordi appena piu' scuri
        const float e = jmin(w, h) * 0.10f;
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.12f), x, y, Colours::transparentBlack, x + e, y, false));       g.fillRect(f);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.12f), x + w, y, Colours::transparentBlack, x + w - e, y, false)); g.fillRect(f);
        g.setGradientFill(ColourGradient(Colours::black.withAlpha(0.12f), x, y + h, Colours::transparentBlack, x, y + h - e, false)); g.fillRect(f);
        // riflesso curvo nella meta' alta (vetro bombato)
        {
            Graphics::ScopedSaveState s2(g);
            Path gloss; gloss.addEllipse(x + w * (0.30f - 1.25f), y + h * (-0.55f - 1.05f), w * 2.5f, h * 2.1f);
            g.reduceClipRegion(gloss);
            g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.15f), x, y, Colours::white.withAlpha(0.025f), x, y + h * 0.55f, false));
            g.fillRect(f);
        }
        // punto di luce in alto a sinistra
        {
            const float cx = x + w * 0.16f, cy = y + h * 0.14f, r = w * 0.10f;
            ColourGradient spot(Colours::white.withAlpha(0.30f), cx, cy, Colours::white.withAlpha(0.0f), cx + r, cy, true);
            g.setGradientFill(spot);
            g.fillEllipse(cx - r, cy - h * 0.07f * 1.6f, 2 * r, h * 0.14f * 1.6f);
        }
        // filo di luce sul bordo alto
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(0.0f), x, y, Colours::white.withAlpha(0.35f), x + w * 0.3f, y, false));
        g.drawLine(x + 6.0f, y + 2.5f, x + w - 6.0f, y + 2.5f, 1.4f);
        // due strisce di riflesso diagonali (u*0.9 + v*0.6 = c)
        auto streak = [&](float c, float thick, float alpha)
        {
            const float u0 = c / 0.9f, u1 = (c - 0.6f * 0.9f) / 0.9f;
            g.setGradientFill(ColourGradient(Colours::white.withAlpha(alpha), x + u0 * w, y, Colours::white.withAlpha(0.0f), x + u1 * w, y + h * 0.9f, false));
            g.drawLine(x + u0 * w, y, x + u1 * w, y + h * 0.9f, thick * w);
        };
        streak(0.74f, 0.030f, 0.17f);
        streak(0.80f, 0.012f, 0.12f);
    }

    void layout(float sc)
    {
        for (auto& p : placed)
        {
            const int m = p.mod == -2 ? moduleAt(p.cx, p.cy) : p.mod;
            const float cx = p.cx + dxOf(m);
            p.c->setBounds(Rectangle<int>((int) std::round((cx - p.w * 0.5f) * sc), (int) std::round((p.cy - p.h * 0.5f) * sc),
                                          (int) std::round(p.w * sc), (int) std::round(p.h * sc)));
        }
    }

    void applyOrder()
    {
        appliedOrder = orderIndex();
        const auto o = chain::order(appliedOrder);
        for (size_t i = 0; i < arrows.size(); ++i)
        {
            const int m = (int) i / 2, dir = (i % 2 == 0) ? -1 : 1, slot = chain::slotOf(o, m);
            arrows[i]->enabledArrow = (dir < 0 && slot > 0) || (dir > 0 && slot < 3);
            arrows[i]->repaint();
        }
        layout((float) ed.getWidth() / gui::kImgW);
        artType = -1;
        ed.repaint();
    }

    void buildArrows()
    {
        static const char* names[4] = { "De-Esser", "FET", "EQ", "Opto" };
        for (int m = 0; m < 4; ++m)
            for (int dir : { -1, 1 })
            {
                auto a = std::make_unique<ModuleArrow>(dir);
                a->setTooltip(String(dir < 0 ? "Sposta a sinistra: " : "Sposta a destra: ") + names[m]
                              + " (cambia anche l'ordine del suono)");
                a->onClick = [this, m, dir]
                {
                    if (auto* p = apvts.getParameter("chain_order"))
                    {
                        const int next = chain::moved(orderIndex(), m, dir);
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(p->convertTo0to1((float) next));
                        p->endChangeGesture();
                    }
                };
                ed.addAndMakeVisible(a.get());
                const float cx = kSlots[m].x + kSlots[m].w * 0.5f + (float) dir * 30.0f;
                placed.push_back({ a.get(), cx, 1300.0f, 34.0f, 34.0f, m });
                arrows.push_back(std::move(a));
            }
        orderAtt = std::make_unique<ParameterAttachment>(*apvts.getParameter("chain_order"), [this](float) { applyOrder(); });
        orderAtt->sendInitialUpdate();
    }

    // LLS 4000: pannello 1108 x 2194 (il doppio del posto nella strip) -> strip
    static Point<float> llsPt(float x, float y) { return { 50.0f + x * (555.0f / 1108.0f), 158.0f + y * 0.5f }; }

    KnobSlider* addLlsKnob(const char* texName, const char* paramId, float x, float y, float r, const char* tip)
    {
        const auto c = llsPt(x, y);
        auto knob = std::make_unique<KnobSlider>(loadImage(texName), Image(), 0.0f);
        knob->setRotaryRange(-134.0f, 134.0f);
        knob->setTooltip(tip);
        auto* raw = knob.get();
        ed.addAndMakeVisible(raw);
        raw->setPopupDisplayEnabled(true, false, &ed);
        sliderAtt.push_back(std::make_unique<SliderParameterAttachment>(*apvts.getParameter(paramId), *raw));
        if (auto* p = apvts.getParameter(paramId))
            raw->setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        placed.push_back({ raw, c.x, c.y, r, r, -1 });
        preLlsComps.push_back(raw);
        knobs.push_back(std::move(knob));
        return raw;
    }

    HotButton* addLlsButton(const char* name, const char* paramId, float x0, float y0, float x1, float y1, const char* tip)
    {
        const auto a = llsPt(x0, y0), b = llsPt(x1, y1);
        auto* btn = addButton(HotButton::Kind::Sprite, "", (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, b.x - a.x, b.y - a.y, tip, paramId);
        btn->spriteOn  = loadImage(String("lls_") + name + "_on_png");
        btn->spriteOff = loadImage(String("lls_") + name + "_off_png");
        placed.back().mod = -1;
        preLlsComps.push_back(btn);
        return btn;
    }

    void buildLls()
    {
        llsPanel   = loadImage("lls_panel_jpg");
        thumbValve = loadImage("pre_thumb_valve_jpg");
        thumbLls   = loadImage("pre_thumb_lls_jpg");
        ledGreenOn = loadImage("lls_led_green_on_png");  ledGreenOff = loadImage("lls_led_green_off_png");
        ledRedOn   = loadImage("lls_led_red_on_png");    ledRedOff   = loadImage("lls_led_red_off_png");
        buildRedFaces();

        // manopole (centro e raggio misurati sul pannello 1108 x 2194)
        addLlsKnob("knob_lls_gain_png",   "lls_gain",   317.3f, 1225.5f, 150.0f, "GAIN (LEVEL): quanto lavora il banco LLS, col livello compensato");
        addLlsKnob("knob_lls_output_png", "lls_output", 800.2f, 1220.7f, 141.0f, "OUTPUT (5 = 0 dB)");
        {
            const auto c = llsPt(557.0f, 1597.0f);
            auto* k = addSteppedKnob("knob_lls_hpf_png", "lls_hpf", c.x, c.y, 120.0f, 0.0f, { 0.f, 1.f, 2.f, 3.f, 4.f },
                                     { -131.5f, -65.5f, 0.0f, 65.5f, 131.0f }, { "Off", "40 Hz", "80 Hz", "150 Hz", "300 Hz" }, "HPF");
            placed.back().mod = -1;
            preLlsComps.push_back(k);
        }
        addLlsButton("pad",   "lls_pad",   558, 793, 702, 932,   "PAD -20 dB");
        addLlsButton("phase", "lls_phase", 800, 793, 942, 932,   "Fase (inverte la polarita')");
        addLlsButton("mic",   "lls_mic",   483, 1903, 627, 2052, "MIC (acceso): il banco lavora di piu' / LINE (spento)");
        auto* byp = addButton(HotButton::Kind::Pill, "BYP", 435, 180, 56, 27, "Bypass (acceso = escluso)", "pre_bypass");
        byp->onColour = Colour(0xffff5040);
        preLlsComps.push_back(byp);

        // linguetta nell'angolo in basso a destra del preamp: Valve o LLS 4000
        preCurl = std::make_unique<EqCurl>();
        preCurl->setTooltip("Cambia preamp: Valve o LLS 4000");
        ed.addAndMakeVisible(preCurl.get());
        placed.push_back({ preCurl.get(), 569.0f, 1220.0f, 70.0f, 70.0f, -1 });
        preCurl->onClick = [this]
        {
            auto chooser = std::make_unique<EqChooser>(thumbValve, thumbLls, preType(), tfBold, tfRegular, [this](int t)
            {
                if (auto* p = apvts.getParameter("pre_type"))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1((float) t));
                    p->endChangeGesture();
                }
            });
            chooser->title = "PREAMP TYPE";
            chooser->names[0] = "VALVE PREAMP"; chooser->subs[0] = "Marley tube";
            chooser->names[1] = "LLS 4000";     chooser->subs[1] = "DirtyDESK LLS console";
            CallOutBox::launchAsynchronously(std::move(chooser), preCurl->getScreenBounds(), nullptr);
        };
        preTypeAtt = std::make_unique<ParameterAttachment>(*apvts.getParameter("pre_type"), [this](float) { applyPreType(); });
        preTypeAtt->sendInitialUpdate();
    }

    void applyPreType()
    {
        const int t = preType();
        appliedPreType = t;
        for (auto* c : preValveComps) c->setVisible(t == 0);
        for (auto* c : preLlsComps)   c->setVisible(t == 1);
        if (preCurl != nullptr)
        {
            preCurl->frontCol = t == 1 ? Colour(0xff2c2d30) : Colour(0xff1f2b45);
            preCurl->underCol = t == 1 ? Colour(0xff1f2b45) : Colour(0xff2c2d30);
            preCurl->repaint();
        }
        artType = -1;
        ed.repaint();
    }

    void updateMeters()
    {
        for (auto& m : meters)
        {
            const auto& mod = ed.proc.getModule(m.stage);
            float target;
            if (m.isGainReduction)   // riposo = 0 (lancetta ferma sullo 0); scende verso sinistra con la riduzione
                target = jlimit(-20.0f, 0.0f, mod.getGainReductionDb());
            else
                target = jlimit(-20.0f, 3.0f, mod.getVuLevel());   // preamp: ingresso valvola (Marley); master: 0 VU = -18 dBFS
            // ballistica VU (~300 ms); in discesa un po' piu' rapida per i compressori
            m.pos += (m.isGainReduction && target < m.pos ? 0.45f : 0.3f) * (target - m.pos);
        }
        llsMeter.pos = meters[0].pos;   // stesso modulo: il preamp sceglie lui cosa misurare
    }
};

//==============================================================================
LunchStripEditor::LunchStripEditor(LunchStripProcessor& p)
    : AudioProcessorEditor(&p), proc(p), impl(std::make_unique<Impl>(*this))
{
    impl->build();

    constexpr int w = 1600;   // 61% dell'immagine: scritte leggibili anche alla misura iniziale
    setResizeLimits(656, (int) (656 * gui::kImgH / gui::kImgW), 2624, 1404);
    getConstrainer()->setFixedAspectRatio((double) gui::kImgW / (double) gui::kImgH);
    setResizable(true, true);
    setOpaque(true);   // lo sfondo copre tutta la finestra: l'host non deve ridisegnare dietro
    setSize(w, (int) (w * gui::kImgH / gui::kImgW));

    startTimerHz(30);
}

LunchStripEditor::~LunchStripEditor() { stopTimer(); }

void LunchStripEditor::paint(Graphics& g)
{
    // Tutta la grafica statica (sfondo, scritte, scale, ombre) viene disegnata UNA volta in un'immagine alla
    // risoluzione reale dello schermo, e rifatta solo se cambia la misura della finestra o il tipo di EQ.
    if (! impl->background.isValid()) { g.fillAll(Colours::black); return; }
    const float phys = jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int pw = jmax(1, roundToInt((float) getWidth() * phys)), ph = jmax(1, roundToInt((float) getHeight() * phys));
    // la chiave della cache: tipo di EQ, tipo di preamp e ordine dei moduli
    const int type = impl->eqType();
    const int key  = type + 2 * impl->preType() + 4 * impl->orderIndex();
    if (! impl->artCache.isValid() || impl->artCache.getWidth() != pw || impl->artCache.getHeight() != ph || impl->artType != key)
    {
        impl->artCache = Image(Image::RGB, pw, ph, true);
        Graphics ig(impl->artCache);
        ig.setImageResamplingQuality(Graphics::highResamplingQuality);
        ig.drawImageAt(impl->background.rescaled(pw, ph, Graphics::highResamplingQuality), 0, 0);
        const float kx = (float) pw / gui::kImgW, ky = (float) ph / gui::kImgH;
        auto drawPanel = [&](const Image& img, Rectangle<float> dest)
        {
            const auto r = Rectangle<float>(dest.getX() * kx, dest.getY() * ky, dest.getWidth() * kx, dest.getHeight() * ky).getSmallestIntegerContainer();
            ig.drawImageAt(img.rescaled(r.getWidth(), r.getHeight(), Graphics::highResamplingQuality), r.getX(), r.getY());
        };

        // moduli 2..5 nell'ordine scelto: si ritagliano dallo sfondo e si rimettono al loro posto nuovo
        if (impl->orderIndex() != 0)
        {
            using I = LunchStripEditor::Impl;
            ig.setColour(Colour(0xff0a0a0b));
            ig.fillRect(Rectangle<float>((I::kSlotsLeft - 6.0f) * kx, (I::kSlotY - 4.0f) * ky,
                                         (I::kSlotsRight - I::kSlotsLeft + 12.0f) * kx, (I::kSlotH + 8.0f) * ky));
            for (int m = 0; m < 4; ++m)
            {
                const auto& sl = I::kSlots[m];
                const auto src = impl->background.getClippedImage(Rectangle<int>((int) sl.x, (int) I::kSlotY, (int) sl.w, (int) I::kSlotH));
                drawPanel(src, { sl.x + impl->dxOf(m), I::kSlotY, sl.w, I::kSlotH });
            }
        }
        if (type == 1 && impl->pultecPanel.isValid())
            drawPanel(impl->pultecPanel, { 1374.0f + impl->dxOf(2), 157.0f, 400.0f, 1098.0f });
        if (impl->preType() == 1 && impl->llsPanel.isValid())
            drawPanel(impl->llsPanel, { 50.0f, 158.0f, 555.0f, 1097.0f });

        ig.addTransform(AffineTransform::scale(kx, ky));
        impl->renderArt(ig, type);
        impl->artType = key;
    }
    g.drawImage(impl->artCache, getLocalBounds().toFloat());
}

void LunchStripEditor::paintOverChildren(Graphics& g)
{
    const float sc = (float) getWidth() / gui::kImgW;
    g.addTransform(AffineTransform::scale(sc));

    // meter a LED del de-esser (0, -3, -6, -12, -20): si accendono dall'alto con la riduzione
    {
        static const float ys[5]  = { 304.5f, 351.4f, 397.5f, 442.0f, 486.5f };
        static const float thr[5] = { 0.5f, 3.f, 6.f, 12.f, 20.f };
        static const uint32_t col[5] = { 0xff40d060, 0xff40d060, 0xffe0c030, 0xffe08030, 0xffe04030 };
        const float red = -proc.getModule(LunchStripProcessor::De).getGainReductionDb();
        Graphics::ScopedSaveState ssDe(g);
        g.addTransform(AffineTransform::translation(impl->dxOf(0), 0.0f));   // il de-esser puo' essere spostato
        for (int i = 0; i < 5; ++i)
        {
            g.setColour(Colour(0xff0d0b0a));
            g.fillEllipse(762.0f - 15.0f, ys[i] - 15.0f, 30.0f, 30.0f);
            const bool lit = red >= thr[i];
            if (lit)
            {
                g.setGradientFill(ColourGradient(Colour(col[i]).withAlpha(0.6f), 762.0f, ys[i], Colour(col[i]).withAlpha(0.0f), 762.0f + 30.0f, ys[i], true));
                g.fillEllipse(762.0f - 30.0f, ys[i] - 30.0f, 60.0f, 60.0f);
            }
            g.setColour(lit ? Colour(col[i]).brighter(0.3f) : Colour(0xff25201c));
            g.fillEllipse(762.0f - 10.0f, ys[i] - 10.0f, 20.0f, 20.0f);
        }
    }

    // valvola del preamp (solo Valve) e VU del preamp che arrossisce con troppa saturazione (Valve e LLS)
    if (impl->preType() == 0) impl->paintTube(g);
    impl->paintVuRed(g);

    // LLS 4000: LED SIGNAL (verde) e PEAK (rosso)
    if (impl->preType() == 1)
    {
        const auto& pre = proc.getModule(LunchStripProcessor::Pre);
        const auto gC = Impl::llsPt(216.8f, 826.2f), rC = Impl::llsPt(427.1f, 904.5f);
        const float r = 13.0f;
        const auto& gi = pre.getSignalLed() ? impl->ledGreenOn : impl->ledGreenOff;
        const auto& ri = pre.getPeakLed()   ? impl->ledRedOn   : impl->ledRedOff;
        g.setImageResamplingQuality(Graphics::highResamplingQuality);
        if (gi.isValid()) g.drawImage(gi, { gC.x - r, gC.y - r, 2 * r, 2 * r });
        if (ri.isValid()) g.drawImage(ri, { rC.x - r, rC.y - r, 2 * r, 2 * r });
    }

    // LED del POWER del Pultec: spento quando l'EQ e' in bypass
    if (impl->eqType() == 1 && impl->apvts.getRawParameterValue("eq_bypass")->load() > 0.5f)
    {
        constexpr float k = 400.0f / 624.0f;
        const float cx = 1374.0f + impl->dxOf(2) + 312.0f * k, cy = 157.0f + 1555.0f * k;   // lente del LED POWER
        g.setGradientFill(ColourGradient(Colour(0xff1a0806), cx, cy, Colour(0x001a0806), cx + 36.0f, cy, true));
        g.fillEllipse(cx - 36.0f, cy - 36.0f, 72.0f, 72.0f);
        g.setColour(Colour(0xff3a1410));
        g.fillEllipse(cx - 17.0f, cy - 17.0f, 34.0f, 34.0f);
        g.setColour(Colours::white.withAlpha(0.12f));
        g.fillEllipse(cx - 8.0f, cy - 11.0f, 11.0f, 7.0f);
    }

    // lancette dei VU
    for (size_t mi = 0; mi < impl->meters.size(); ++mi)
    {
        const auto& m = mi == 0 ? impl->preMeter() : impl->meters[mi];
        Graphics::ScopedSaveState ss(g);
        g.addTransform(AffineTransform::translation(impl->dxOf(Impl::kMeterMod[mi]), 0.0f));
        g.reduceClipRegion(m.clip.toNearestInt());
        const float a = degreesToRadians(m.angleDeg());
        const float tx = m.px + m.len * std::sin(a), ty = m.py - m.len * std::cos(a);
        g.setColour(Colours::black.withAlpha(0.28f));
        g.drawLine(m.px + 3.0f, m.py + 4.0f, tx + 3.0f, ty + 4.0f, 4.0f);
        g.setColour(Colour(0xff17110d));
        g.drawLine(m.px, m.py, tx, ty, 3.2f);
    }
    // vetro sopra i VU (la lancetta resta dietro), spostato col modulo se FET/Opto sono stati scambiati
    for (const auto& f : impl->glassFaces())
    {
        Graphics::ScopedSaveState ss(g);
        g.addTransform(AffineTransform::translation(impl->dxOf(f.mod), 0.0f));
        Impl::paintGlass(g, f.r, f.radius);
    }

}

void LunchStripEditor::resized()
{
    impl->layout((float) getWidth() / gui::kImgW);
    impl->drawnMeter.fill(-1.0f);
    impl->drawnGlow = impl->drawnRed = -1.0f;
    impl->drawnLeds = -1;
}

void LunchStripEditor::timerCallback()
{
    if (! isShowing()) return;   // finestra chiusa o nascosta: niente lavoro

    // controllo di sicurezza: se il tipo di EQ e' cambiato (preset, automazione, DAW che ricarica lo stato)
    // e i controlli visibili non corrispondono, li rimette a posto. Cosi' manopole e pannello non si mescolano mai.
    if (impl->appliedEqType != impl->eqType())
        impl->applyEqType();
    if (impl->appliedPreType != impl->preType())
        impl->applyPreType();
    if (impl->appliedOrder != impl->orderIndex())
        impl->applyOrder();

    impl->updateMeters();
    impl->updateTube(1.0f / 30.0f);
    impl->updateVuRed();
    const float sc = (float) getWidth() / gui::kImgW;
    auto area = [sc](Rectangle<float> r) { return r.transformedBy(AffineTransform::scale(sc)).getSmallestIntegerContainer().expanded(2); };

    // lancette (e VU rosso del preamp): solo se si sono mosse
    for (size_t i = 0; i < impl->meters.size(); ++i)
    {
        const auto& m = i == 0 ? impl->preMeter() : impl->meters[i];
        const bool redChanged = i == 0 && std::abs(impl->vuRed - impl->drawnRed) > 0.004f;
        if (std::abs(m.pos - impl->drawnMeter[i]) > 0.02f || redChanged)
        {
            impl->drawnMeter[i] = m.pos;
            if (i == 0) impl->drawnRed = impl->vuRed;
            repaint(area(m.clip.translated(impl->dxOf(Impl::kMeterMod[i]), 0.0f).getUnion(i == 0 ? impl->preFace() : m.clip)));
        }
    }

    // LED del de-esser: solo se cambia quanti sono accesi
    {
        static const float thr[5] = { 0.5f, 3.f, 6.f, 12.f, 20.f };
        const float red = -proc.getModule(LunchStripProcessor::De).getGainReductionDb();
        int lit = 0;
        for (float t : thr) lit += red >= t ? 1 : 0;
        if (lit != impl->drawnLeds)
        {
            impl->drawnLeds = lit;
            repaint(area(Rectangle<float>(725.0f, 268.0f, 74.0f, 250.0f).translated(impl->dxOf(0), 0.0f)));
        }
    }

    // LED dell'LLS 4000
    {
        const auto& pre = proc.getModule(LunchStripProcessor::Pre);
        if (pre.getSignalLed() != impl->drawnSignal || pre.getPeakLed() != impl->drawnPeak)
        {
            impl->drawnSignal = pre.getSignalLed(); impl->drawnPeak = pre.getPeakLed();
            repaint(area({ 140.0f, 550.0f, 150.0f, 80.0f }));
        }
    }

    // valvola: solo se la luce e' cambiata in modo visibile
    const float glowNow = impl->heat * impl->flicker * (0.85f + 0.3f * impl->tubeLoad);
    if (std::abs(glowNow - impl->drawnGlow) > 0.004f)
    {
        impl->drawnGlow = glowNow;
        repaint(area({ 100.0f, 470.0f, 270.0f, 250.0f }));   // valvola + alone
    }
}
