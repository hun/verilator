-- DESCRIPTION: Verilator: VHDL generic subprogram item without a separator
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- A generic subprogram item must end in ';' or ')'. Here another name follows the '<>' default with no separator.

entity t is
  generic (function f return integer is <> x);
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
