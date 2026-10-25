// Country flags, evaluated in flag coordinates rather than texels.
// Coverage follows the pixel derivatives, so internal lines, stars and the
// silhouette stay sharp through window resizing and fractional menu scaling.
// Country ids follow GfxCountryFlag in port/gfx.h. No bitmap assets are used.

float FlagCoverage(float distance)
{
    return clamp(0.5 - distance / max(fwidth(distance), 1e-6), 0.0, 1.0);
}

float FlagBox(vec2 p, vec2 lo, vec2 hi)
{
    vec2 d = max(lo - p, p - hi);
    return max(d.x, d.y);
}

float FlagSegment(vec2 p, vec2 a, vec2 b, float radius)
{
    vec2 edge = b - a;
    return length(p - a - edge * clamp(dot(p - a, edge) / dot(edge, edge), 0.0, 1.0)) - radius;
}

// Distance to the nearest of the two edges in a folded star sector.
float FlagStar(vec2 p, vec2 centre, float radius, int points)
{
    const float pi = 3.14159265359;
    p -= centre;
    float sector = 2.0 * pi / float(points);
    float angle = abs(mod(atan(p.x, -p.y + 1e-12) + sector * 0.5, sector) - sector * 0.5);
    p = length(p) * vec2(cos(angle), sin(angle));
    vec2 a = vec2(radius, 0.0);
    vec2 b = radius * (4.0 / 9.0) *
             (points == 7 ? vec2(0.900968868, 0.433883739) : vec2(0.809016994, 0.587785252));
    vec2 edge = b - a;
    vec2 q = p - a;
    float distance = length(q - edge * clamp(dot(q, edge) / dot(edge, edge), 0.0, 1.0));
    return distance * sign(q.x * edge.y - q.y * edge.x);
}

vec3 FlagUnionJack(vec2 uv)
{
    vec2 p = uv * vec2(60.0, 30.0);
    vec3 blue = vec3(1.0, 33.0, 105.0) / 255.0;
    vec3 red = vec3(200.0, 16.0, 46.0) / 255.0;
    float diagonalA = (p.y - p.x * 0.5) / sqrt(1.25);
    float diagonalB = (p.y + p.x * 0.5 - 30.0) / sqrt(1.25);
    float whiteDiagonals = min(abs(diagonalA), abs(diagonalB)) - 3.0;
    // The red saltire is offset within the white, with opposite offsets in
    // opposite halves: the Union Jack is not mirror symmetric.
    float offset = p.x < 30.0 ? 1.0 : -1.0;
    float redDiagonals = min(abs(diagonalA - offset), abs(diagonalB - offset)) - 1.0;
    vec3 color = mix(blue, vec3(1.0), FlagCoverage(whiteDiagonals));
    color = mix(color, red, FlagCoverage(redDiagonals));
    float cross = min(abs(p.x - 30.0), abs(p.y - 15.0));
    color = mix(color, vec3(1.0), FlagCoverage(cross - 5.0));
    return mix(color, red, FlagCoverage(cross - 3.0));
}

vec3 FlagAustralia(vec2 uv)
{
    // Construction: Australian Flags booklet, PM&C, pp. 4-5.
    // https://www.pmc.gov.au/resources/australian-flags-booklet/part-1-australian-national-flag
    vec3 color = vec3(1.0, 33.0, 105.0) / 255.0;
    float canton = FlagBox(uv, vec2(0.0), vec2(0.5));
    color = mix(color, FlagUnionJack(uv * 2.0), FlagCoverage(canton));
    vec2 p = uv * vec2(2.0, 1.0);
    float stars = FlagStar(p, vec2(0.5, 0.75), 0.15, 7);
    stars = min(stars, FlagStar(p, vec2(1.5, 5.0 / 6.0), 1.0 / 14.0, 7));
    stars = min(stars, FlagStar(p, vec2(1.25, 7.0 / 16.0), 1.0 / 14.0, 7));
    stars = min(stars, FlagStar(p, vec2(1.5, 1.0 / 6.0), 1.0 / 14.0, 7));
    stars = min(stars, FlagStar(p, vec2(31.0 / 18.0, 19.0 / 48.0), 1.0 / 14.0, 7));
    stars = min(stars, FlagStar(p, vec2(1.6, 7.0 / 12.0), 1.0 / 24.0, 5));
    return mix(color, vec3(1.0), FlagCoverage(stars));
}

float FlagSpear(vec2 p, float direction)
{
    vec2 a = vec2(3.4 * direction, -7.3), b = vec2(-3.0 * direction, 7.0);
    float shaft = FlagSegment(p, a, b, 0.085);
    vec2 axis = normalize(a - b);
    vec2 q = vec2(dot(p - a, vec2(axis.y, -axis.x)), dot(p - a, axis));
    // The spearhead is a slender diamond, centred on the shaft's upper tip.
    float head = (abs(q.x) / 0.33 + abs(q.y) / 1.2 - 1.0) * 0.33;
    return min(shaft, head);
}

vec3 FlagKenya(vec2 uv)
{
    // Shield and crossed spears: Constitution of Kenya, Second Schedule.
    // https://klrc.go.ke/index.php/constitution-of-kenya/163-schedules-schedules/164-second-schedule-national-symbols/435-a-the-national-flag
    vec2 p = (uv - 0.5) * vec2(30.0, 20.0);
    vec3 red = vec3(187.0, 0.0, 0.0) / 255.0;
    vec3 green = vec3(0.0, 102.0, 0.0) / 255.0;
    vec3 color = mix(vec3(0.0), green, FlagCoverage(-p.y));
    color = mix(color, vec3(1.0), FlagCoverage(abs(p.y) - 4.0));
    color = mix(color, red, FlagCoverage(abs(p.y) - 3.0));
    color = mix(color, vec3(1.0), FlagCoverage(min(FlagSpear(p, 1.0), FlagSpear(p, -1.0))));
    // A pointed shield, the intersection of two circular arcs. The red
    // centre, black flanks and white centre details are separate shapes.
    float shield = max(length(p - vec2(8.0, 0.0)), length(p + vec2(8.0, 0.0))) - 10.0;
    vec3 face = mix(vec3(0.0), red, FlagCoverage(abs(p.x) - 1.25));
    float slit = FlagBox(p, vec2(-0.065, -5.8), vec2(0.065, 5.8));
    float centre = (length(p / vec2(0.28, 0.8)) - 1.0) * 0.28;
    face = mix(face, vec3(1.0), FlagCoverage(min(slit, centre)));
    return mix(color, face, FlagCoverage(shield));
}

vec4 SampleVectorFlag(vec2 uv, int country)
{
    vec3 color;
    if (country == 1 || country == 4) {
        // Nordic crosses: Finland 18:11 (5+3+10, 4+3+4), Sweden
        // 16:10 (5+2+9, 4+2+4). The icon keeps its original cell's shape.
        vec2 p = uv * (country == 1 ? vec2(18.0, 11.0) : vec2(16.0, 10.0));
        float thickness = country == 1 ? 3.0 : 2.0;
        float cross = min(max(5.0 - p.x, p.x - 5.0 - thickness),
                          max(4.0 - p.y, p.y - 4.0 - thickness));
        vec3 blue = (country == 1 ? vec3(0.0, 47.0, 108.0) : vec3(0.0, 106.0, 167.0)) / 255.0;
        color = country == 1 ? mix(vec3(1.0), blue, FlagCoverage(cross))
                             : mix(blue, vec3(254.0, 204.0, 0.0) / 255.0, FlagCoverage(cross));
    } else if (country == 2) {
        vec2 p = uv * vec2(13.5, 9.0);
        vec3 blue = vec3(13.0, 94.0, 175.0) / 255.0;
        float stripe = abs(mod(p.y - 0.5, 2.0) - 1.0) - 0.5;
        color = mix(blue, vec3(1.0), FlagCoverage(stripe));
        float cross = min(abs(p.x - 2.5), abs(p.y - 2.5)) - 0.5;
        vec3 canton = mix(blue, vec3(1.0), FlagCoverage(cross));
        color = mix(color, canton, FlagCoverage(FlagBox(p, vec2(0.0), vec2(5.0))));
    } else if (country == 3 || country == 7) {
        vec3 left = (country == 3 ? vec3(0.0, 35.0, 149.0) : vec3(0.0, 146.0, 70.0)) / 255.0;
        vec3 right = (country == 3 ? vec3(237.0, 41.0, 57.0) : vec3(206.0, 43.0, 55.0)) / 255.0;
        color = mix(left, vec3(1.0), FlagCoverage(1.0 / 3.0 - uv.x));
        color = mix(color, right, FlagCoverage(2.0 / 3.0 - uv.x));
    } else if (country == 5) {
        color = FlagAustralia(uv);
    } else if (country == 6) {
        color = FlagKenya(uv);
    } else if (country == 9) {
        color = mix(vec3(0.0), vec3(221.0, 0.0, 0.0) / 255.0, FlagCoverage(1.0 / 3.0 - uv.y));
        color = mix(color, vec3(255.0, 206.0, 0.0) / 255.0, FlagCoverage(2.0 / 3.0 - uv.y));
    } else if (country == 10) {
        // The civil triband, as on the original language-selection matrix.
        color = mix(vec3(170.0, 21.0, 27.0) / 255.0, vec3(241.0, 191.0, 0.0) / 255.0,
                    FlagCoverage(max(0.25 - uv.y, uv.y - 0.75)));
    } else {
        color = FlagUnionJack(uv);
    }
    return vec4(color, FlagCoverage(FlagBox(uv, vec2(0.0), vec2(1.0))));
}
