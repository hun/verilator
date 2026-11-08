-- DESCRIPTION: Verilator: VHDL entity elaboration errors
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity lonely is
end;

entity multi is
end;

architecture a1 of multi is
begin
end;

architecture a2 of multi is
begin
end;
