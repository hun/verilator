-- DESCRIPTION: Verilator: VHDL mode view port followed by a name with no separator
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- A port clause item must end in ';' or ')'. The next item follows the view indication with no separator.

entity t is
  port (p : view vt q : in bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
