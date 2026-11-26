-- DESCRIPTION: Verilator: VHDL external name with an unknown class
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An external name starts with the class of the object it names: signal, constant or
-- variable (IEEE 1076-2008 8.7). 'entity' is not a class.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= << entity t.x : bit >>;
end architecture rtl;
