-- DESCRIPTION: Verilator: VHDL design unit and declaration diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Duplicate design units, a mismatched architecture, and duplicate or invalid declarations.

package p is
end package;

package body p is
end package body;

package body p is
end package body;

package q is
end package;

package body q is
  constant c : integer;
end package body;

entity dupe is
end entity;

entity dupe is
end entity;

architecture a of nosuch is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
  signal s : bit;
  signal s : bit;
  subtype u is bit range 0 to 1;
  signal t : bit_vector(nosuch);
begin
end architecture;
